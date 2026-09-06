#include "flexbms/GatewayApi.h"
#include "flexbms/GatewayAssets.h"
#include "flexbms/FirmwareUpdate.h"
#include "flexbms/MqttClient.h"
#include "flexbms/TimeSync.h"
#include "flexbms/WifiManager.h"

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>

namespace FlexBms::GatewayApi
{
    namespace
    {
        constexpr const char *kLogTag = "flexbms_gateway_api";
        constexpr size_t kMaxBrowserMessageBytes = 4096U;
        constexpr size_t kMaxRequestIdBytes = 64U;
        constexpr size_t kMaxServiceArgumentBytes = 16U;
        constexpr size_t kMaxSlaves = 32U;
        constexpr size_t kMaxHttpClients = 4U;
        constexpr size_t kMaxTrackedWebSockets = 4U;
        constexpr uint8_t kFirmwareReceiveTimeoutLimit = 3U;
        httpd_handle_t server = nullptr;
        ServiceSender serviceSender = nullptr;
        bool uartHealthy = false;
        bool serviceInFlight = false;
        bool serviceCapabilityKnown = false;
        bool serviceCapability = false;
        uint8_t pendingSequence = 0U;
        uint8_t nextServiceSequence = 1U;
        int pendingServiceSocket = -1;
        int64_t pendingServiceStartedUs = 0;
        char pendingRequestId[kMaxRequestIdBytes + 1U] = {};
        Service pendingService = Service::SetRunRequest;
        InternalServiceCompletion internalServiceCompletion = nullptr;
        int pendingScanSocket = -1;
        char pendingScanRequestId[kMaxRequestIdBytes + 1U] = {};
        UartV1::Status status{};
        UartV1::Pack pack{};
        UartV1::Energy energy{};
        UartV1::HvVoltages hvVoltages{};
        std::array<UartV1::Cell, kMaxSlaves> cells{};
        std::array<UartV1::Temperature, kMaxSlaves> temperatures{};
        std::array<bool, kMaxSlaves> hasCell{};
        std::array<bool, kMaxSlaves> hasTemperature{};
        bool hasStatus = false;
        bool hasPack = false;
        bool hasEnergy = false;
        bool hasHvVoltages = false;

        struct WebSocketDelivery
        {
            int socket = -1;
            bool sendPending = false;
            bool retiring = false;
            int64_t nextCloseAttemptUs = 0;
            char *queuedText = nullptr;
            size_t queuedLength = 0U;
            uint32_t generation = 0U;
        };
        std::array<WebSocketDelivery, kMaxTrackedWebSockets> webSocketDeliveries{};
        SemaphoreHandle_t webSocketDeliveryMutex = nullptr;
        SemaphoreHandle_t serviceStateMutex = nullptr;
        SemaphoreHandle_t telemetryStateMutex = nullptr;

        void lockServiceState()
        {
            if (serviceStateMutex != nullptr) (void)xSemaphoreTake(serviceStateMutex, portMAX_DELAY);
        }

        void unlockServiceState()
        {
            if (serviceStateMutex != nullptr) (void)xSemaphoreGive(serviceStateMutex);
        }

        void lockTelemetryState()
        {
            if (telemetryStateMutex != nullptr) (void)xSemaphoreTake(telemetryStateMutex, portMAX_DELAY);
        }

        void unlockTelemetryState()
        {
            if (telemetryStateMutex != nullptr) (void)xSemaphoreGive(telemetryStateMutex);
        }

        bool telemetryHasStatus()
        {
            lockTelemetryState();
            const bool available = hasStatus;
            unlockTelemetryState();
            return available;
        }

        const char *wifiState()
        {
            switch (Wifi::getState())
            {
            case Wifi::State::Provisioning: return "provisioning";
            case Wifi::State::Connecting: return "connecting";
            case Wifi::State::Connected: return "connected";
            case Wifi::State::Recovery: return "recovery";
            default: return "unavailable";
            }
        }

        const char *serviceName(Service service)
        {
            switch (service)
            {
            case Service::SetRunRequest: return "set_run_request";
            case Service::SetBalancingEnabled: return "set_balancing_enabled";
            case Service::AcknowledgeFaults: return "acknowledge_faults";
            case Service::SetRtc: return "set_rtc";
            case Service::GetRtc: return "get_rtc";
            case Service::GetDeviceInfo: return "get_device_info";
            case Service::ReadRegister: return "read_register";
            case Service::GetConfig: return "get_config";
            case Service::SetConfig: return "set_config";
            case Service::GetDiagnosticReport: return "get_diagnostic_report";
            }
            return "";
        }

        const char *resultName(ServiceResult result)
        {
            switch (result)
            {
            case ServiceResult::Ok: return "ok";
            case ServiceResult::Denied: return "denied";
            case ServiceResult::Invalid: return "invalid";
            case ServiceResult::Busy: return "busy";
            case ServiceResult::UsbHostActive: return "usb_host_active";
            default: return "transport_error";
            }
        }

        const char *updatePhaseName(FirmwareUpdate::Phase phase)
        {
            switch (phase)
            {
            case FirmwareUpdate::Phase::Uploading: return "uploading";
            case FirmwareUpdate::Phase::Installing: return "installing";
            case FirmwareUpdate::Phase::Complete: return "complete";
            case FirmwareUpdate::Phase::Failed: return "failed";
            default: return "idle";
            }
        }

        const char *updateTargetName(FirmwareUpdate::Target target)
        {
            return target == FirmwareUpdate::Target::Gateway ? "gateway" : "stm32";
        }

        const char *updateStageName(FirmwareUpdate::Stage stage)
        {
            switch (stage)
            {
            case FirmwareUpdate::Stage::Upload: return "upload";
            case FirmwareUpdate::Stage::Validate: return "validate";
            case FirmwareUpdate::Stage::Restart: return "restart";
            case FirmwareUpdate::Stage::Handoff: return "handoff";
            case FirmwareUpdate::Stage::RomBootloader: return "rom";
            case FirmwareUpdate::Stage::Erase: return "erase";
            case FirmwareUpdate::Stage::Program: return "program";
            case FirmwareUpdate::Stage::Verify: return "verify";
            case FirmwareUpdate::Stage::Complete: return "complete";
            default: return "idle";
            }
        }

        struct QueuedWebSocketText
        {
            char *text = nullptr;
            httpd_ws_frame_t frame{};
            uint32_t generation = 0U;
        };

        WebSocketDelivery *findWebSocketDelivery(int socket, bool create)
        {
            for (auto &delivery : webSocketDeliveries)
            {
                if (delivery.socket == socket) return &delivery;
            }
            if (!create) return nullptr;
            for (auto &delivery : webSocketDeliveries)
            {
                if (delivery.socket < 0)
                {
                    delivery.socket = socket;
                    ++delivery.generation;
                    if (delivery.generation == 0U) delivery.generation = 1U;
                    return &delivery;
                }
            }
            return nullptr;
        }

        void resetWebSocketDelivery(WebSocketDelivery &delivery)
        {
            const uint32_t nextGeneration = delivery.generation + 1U;
            std::free(delivery.queuedText);
            delivery = {};
            delivery.generation = nextGeneration == 0U ? 1U : nextGeneration;
        }

        void lockWebSocketDeliveries()
        {
            if (webSocketDeliveryMutex != nullptr) (void)xSemaphoreTake(webSocketDeliveryMutex, portMAX_DELAY);
        }

        void unlockWebSocketDeliveries()
        {
            if (webSocketDeliveryMutex != nullptr) (void)xSemaphoreGive(webSocketDeliveryMutex);
        }

        void prepareWebSocketConnection(int socket)
        {
            lockWebSocketDeliveries();
            WebSocketDelivery *delivery = findWebSocketDelivery(socket, false);
            if (delivery != nullptr) resetWebSocketDelivery(*delivery);
            unlockWebSocketDeliveries();
        }

        void retireUnresponsiveWebSockets()
        {
            if (server == nullptr) return;
            lockWebSocketDeliveries();
            const int64_t now = esp_timer_get_time();
            for (auto &delivery : webSocketDeliveries)
            {
                if (delivery.socket < 0) continue;
                if (httpd_ws_get_fd_info(server, delivery.socket) != HTTPD_WS_CLIENT_WEBSOCKET)
                {
                    resetWebSocketDelivery(delivery);
                    continue;
                }
                if (!delivery.retiring || now < delivery.nextCloseAttemptUs) continue;

                // This runs from the Gateway task, not from the HTTP server's
                // asynchronous-send callback. That keeps the server control
                // queue free to process the requested close.
                delivery.nextCloseAttemptUs = now + 5'000'000LL;
                const esp_err_t closeResult = httpd_sess_trigger_close(server, delivery.socket);
                if (closeResult != ESP_OK && closeResult != ESP_ERR_NOT_FOUND)
                {
                    ESP_LOGW(kLogTag, "WebSocket close request failed for socket %d: %s", delivery.socket, esp_err_to_name(closeResult));
                }
            }
            unlockWebSocketDeliveries();
        }

        void releaseQueuedWebSocketText(esp_err_t result, int socket, void *context)
        {
            auto *message = static_cast<QueuedWebSocketText *>(context);
            const uint32_t generation = message->generation;
            std::free(message->text);
            delete message;
            lockWebSocketDeliveries();
            WebSocketDelivery *delivery = findWebSocketDelivery(socket, false);
            if (delivery != nullptr && delivery->generation == generation)
            {
                delivery->sendPending = false;
                if (result != ESP_OK)
                {
                    // Do not queue another telemetry frame or close request to
                    // this socket. poll() retires it from outside the HTTP
                    // server callback, avoiding a control-queue feedback loop.
                    delivery->retiring = true;
                    delivery->nextCloseAttemptUs = 0;
                    std::free(delivery->queuedText);
                    delivery->queuedText = nullptr;
                    delivery->queuedLength = 0U;
                    ESP_LOGW(kLogTag, "Retiring unresponsive WebSocket client %d: %s", socket, esp_err_to_name(result));
                }
            }
            unlockWebSocketDeliveries();
        }

        bool queueWebSocketText(int socket, const char *text, size_t length)
        {
            if (server == nullptr || text == nullptr ||
                httpd_ws_get_fd_info(server, socket) != HTTPD_WS_CLIENT_WEBSOCKET)
            {
                return false;
            }

            char *copy = static_cast<char *>(std::malloc(length + 1U));
            if (copy == nullptr) return false;
            std::memcpy(copy, text, length);
            copy[length] = '\0';

            lockWebSocketDeliveries();
            WebSocketDelivery *delivery = findWebSocketDelivery(socket, true);
            if (delivery == nullptr || delivery->retiring)
            {
                unlockWebSocketDeliveries();
                std::free(copy);
                return false;
            }

            if (delivery->sendPending || delivery->queuedText != nullptr)
            {
                // Status publications are state, not an event log. Keep the
                // newest one and send it after the current async frame.
                std::free(delivery->queuedText);
                delivery->queuedText = copy;
                delivery->queuedLength = length;
                unlockWebSocketDeliveries();
                return true;
            }

            auto *message = new (std::nothrow) QueuedWebSocketText{};
            if (message == nullptr)
            {
                unlockWebSocketDeliveries();
                std::free(copy);
                return false;
            }
            message->text = copy;
            message->frame.type = HTTPD_WS_TYPE_TEXT;
            message->frame.payload = reinterpret_cast<uint8_t *>(message->text);
            message->frame.len = length;
            message->generation = delivery->generation;
            delivery->sendPending = true;
            const uint32_t generation = delivery->generation;
            unlockWebSocketDeliveries();

            if (httpd_ws_send_data_async(server, socket, &message->frame,
                                         releaseQueuedWebSocketText, message) == ESP_OK)
            {
                return true;
            }

            lockWebSocketDeliveries();
            delivery = findWebSocketDelivery(socket, false);
            if (delivery != nullptr && delivery->generation == generation)
            {
                delivery->sendPending = false;
                delivery->retiring = true;
                delivery->nextCloseAttemptUs = 0;
                std::free(delivery->queuedText);
                delivery->queuedText = nullptr;
                delivery->queuedLength = 0U;
            }
            unlockWebSocketDeliveries();
            std::free(message->text);
            delete message;
            return false;
        }

        void flushQueuedWebSockets()
        {
            if (server == nullptr) return;
            for (auto &delivery : webSocketDeliveries)
            {
                QueuedWebSocketText *message = nullptr;
                int socket = -1;
                uint32_t generation = 0U;
                lockWebSocketDeliveries();
                if (delivery.socket >= 0 && !delivery.sendPending && !delivery.retiring && delivery.queuedText != nullptr)
                {
                    if (httpd_ws_get_fd_info(server, delivery.socket) != HTTPD_WS_CLIENT_WEBSOCKET)
                    {
                        resetWebSocketDelivery(delivery);
                    }
                    else
                    {
                        message = new (std::nothrow) QueuedWebSocketText{};
                        if (message != nullptr)
                        {
                            message->text = delivery.queuedText;
                            message->frame.type = HTTPD_WS_TYPE_TEXT;
                            message->frame.payload = reinterpret_cast<uint8_t *>(message->text);
                            message->frame.len = delivery.queuedLength;
                            message->generation = delivery.generation;
                            socket = delivery.socket;
                            generation = delivery.generation;
                            delivery.queuedText = nullptr;
                            delivery.queuedLength = 0U;
                            delivery.sendPending = true;
                        }
                    }
                }
                unlockWebSocketDeliveries();

                if (message == nullptr) continue;
                if (httpd_ws_send_data_async(server, socket, &message->frame,
                                             releaseQueuedWebSocketText, message) == ESP_OK)
                {
                    continue;
                }

                lockWebSocketDeliveries();
                WebSocketDelivery *current = findWebSocketDelivery(socket, false);
                if (current != nullptr && current->generation == generation)
                {
                    current->sendPending = false;
                    current->retiring = true;
                    current->nextCloseAttemptUs = 0;
                    std::free(current->queuedText);
                    current->queuedText = nullptr;
                    current->queuedLength = 0U;
                }
                unlockWebSocketDeliveries();
                std::free(message->text);
                delete message;
            }
        }

        bool sendJsonTo(int socket, cJSON *root)
        {
            char *text = cJSON_PrintUnformatted(root);
            cJSON_Delete(root);
            if (text == nullptr) return false;
            const bool queued = queueWebSocketText(socket, text, std::strlen(text));
            cJSON_free(text);
            return queued;
        }

        void broadcast(cJSON *root)
        {
            if (server == nullptr) { cJSON_Delete(root); return; }
            retireUnresponsiveWebSockets();
            size_t count = 8U;
            std::array<int, 8U> sockets{};
            if (httpd_get_client_list(server, &count, sockets.data()) != ESP_OK) { cJSON_Delete(root); return; }
            char *text = cJSON_PrintUnformatted(root);
            cJSON_Delete(root);
            if (text == nullptr) return;
            for (size_t index = 0U; index < count; ++index)
            {
                // httpd_get_client_list includes ordinary HTTP sockets. Never
                // write WebSocket framing to those sockets, and queue delivery
                // so a slow browser cannot stall the UART receive loop.
                (void)queueWebSocketText(sockets[index], text, std::strlen(text));
            }
            cJSON_free(text);
        }

        cJSON *base(const char *type)
        {
            cJSON *root = cJSON_CreateObject();
            cJSON_AddNumberToObject(root, "v", 1);
            cJSON_AddStringToObject(root, "type", type);
            return root;
        }

        void addGatewayStatus(cJSON *root)
        {
            cJSON_AddNumberToObject(root, "gateway_uptime_ms", static_cast<double>(esp_timer_get_time() / 1000LL));
            const esp_partition_t *runningPartition = esp_ota_get_running_partition();
            if (runningPartition != nullptr) cJSON_AddStringToObject(root, "gateway_partition", runningPartition->label);
            cJSON_AddStringToObject(root, "wifi_state", wifiState());
            if (Wifi::getState() != Wifi::State::Provisioning && Wifi::getState() != Wifi::State::Unavailable)
            {
                cJSON_AddStringToObject(root, "wifi_ssid", Wifi::getStationSsid());
            }
            const Wifi::AccessPoint accessPoint = Wifi::getAccessPoint();
            cJSON *setup = cJSON_AddObjectToObject(root, "setup_ap");
            cJSON_AddBoolToObject(setup, "active", accessPoint.active);
            if (accessPoint.active)
            {
                cJSON_AddStringToObject(setup, "ssid", accessPoint.ssid.data());
                cJSON_AddStringToObject(setup, "address", accessPoint.address.data());
            }
            lockServiceState();
            const bool currentUartHealthy = uartHealthy;
            unlockServiceState();
            cJSON_AddStringToObject(root, "uart_state", currentUartHealthy ? "healthy" : (telemetryHasStatus() ? "lost" : "starting"));
            cJSON_AddStringToObject(root, "mqtt_state", Mqtt::stateName(Mqtt::getState()));
            const Mqtt::Settings mqtt = Mqtt::getSettings();
            cJSON *mqttJson = cJSON_AddObjectToObject(root, "mqtt");
            cJSON_AddBoolToObject(mqttJson, "configured", mqtt.configured);
            if (mqtt.configured)
            {
                cJSON_AddStringToObject(mqttJson, "host", mqtt.host);
                cJSON_AddNumberToObject(mqttJson, "port", mqtt.port);
                cJSON_AddStringToObject(mqttJson, "username", mqtt.username);
            }
            const TimeSync::Status timeSync = TimeSync::getStatus();
            cJSON *timeSyncJson = cJSON_AddObjectToObject(root, "time_sync");
            cJSON_AddStringToObject(timeSyncJson, "state", TimeSync::stateName(timeSync.state));
            if (timeSync.hasLastSync) cJSON_AddNumberToObject(timeSyncJson, "last_sync_unix_s", timeSync.lastSyncUnixS);
            cJSON *log = cJSON_AddObjectToObject(root, "diagnostic_log");
            cJSON_AddBoolToObject(log, "available", false);
            cJSON_AddNumberToObject(log, "bytes", 0);
            const FirmwareUpdate::Status &update = FirmwareUpdate::getStatus();
            cJSON *updateJson = cJSON_AddObjectToObject(root, "firmware_update");
            cJSON_AddStringToObject(updateJson, "phase", updatePhaseName(update.phase));
            cJSON_AddStringToObject(updateJson, "target", updateTargetName(update.target));
            cJSON_AddStringToObject(updateJson, "stage", updateStageName(update.stage));
            cJSON_AddNumberToObject(updateJson, "received_bytes", update.bytesReceived);
            cJSON_AddNumberToObject(updateJson, "expected_bytes", update.bytesExpected);
            cJSON_AddNumberToObject(updateJson, "progress_bytes", update.progressBytes);
            cJSON_AddStringToObject(updateJson, "version", update.version);
            cJSON_AddStringToObject(updateJson, "detail", update.detail);
        }

        cJSON *hello()
        {
            const bool bmsServices = Wifi::allowsBmsServices();
            cJSON *root = base("hello");
            cJSON_AddStringToObject(root, "gateway_version", GatewayAssets::kCompanionVersion);
            cJSON_AddStringToObject(root, "gateway_build_id", GatewayAssets::kCompanionBuildId);
            cJSON *caps = cJSON_AddObjectToObject(root, "capabilities");
            cJSON_AddBoolToObject(caps, "monitor", true);
            cJSON_AddBoolToObject(caps, "csv_logging", true);
            cJSON_AddBoolToObject(caps, "set_run_request", bmsServices);
            cJSON_AddBoolToObject(caps, "set_balancing_enabled", bmsServices);
            cJSON_AddBoolToObject(caps, "acknowledge_faults", bmsServices);
            cJSON_AddBoolToObject(caps, "get_rtc", bmsServices);
            cJSON_AddBoolToObject(caps, "get_device_info", bmsServices);
            cJSON_AddBoolToObject(caps, "read_register", bmsServices);
            cJSON_AddBoolToObject(caps, "get_diagnostic_report", bmsServices);
            cJSON_AddBoolToObject(caps, "runtime_configuration", bmsServices);
            cJSON_AddBoolToObject(caps, "wifi_configuration", Wifi::getState() != Wifi::State::Unavailable);
            cJSON_AddBoolToObject(caps, "mqtt_configuration", Wifi::allowsBmsServices());
            cJSON_AddBoolToObject(caps, "diagnostic_log_download", false);
            cJSON_AddBoolToObject(caps, "raw_terminal", false);
            cJSON_AddBoolToObject(caps, "firmware_update", FirmwareUpdate::isAvailable());
            cJSON *gateway = cJSON_AddObjectToObject(root, "gateway_status");
            addGatewayStatus(gateway);
            return root;
        }

        bool completeSnapshotLocked()
        {
            if (!hasStatus || !hasPack || status.slaveCount > kMaxSlaves || (status.flags & (1U << 3U)) == 0U)
            {
                return false;
            }
            for (uint8_t index = 0U; index < status.slaveCount; ++index)
            {
                if (!hasCell[index] || !hasTemperature[index])
                {
                    return false;
                }
            }
            return true;
        }

        void addBmsStatus(cJSON *root)
        {
            cJSON_AddNumberToObject(root, "bms_state", status.bmsState);
            cJSON_AddNumberToObject(root, "hv_state", status.hvState);
            cJSON_AddNumberToObject(root, "flags", status.flags);
            cJSON_AddNumberToObject(root, "slave_count", status.slaveCount);
            cJSON_AddNumberToObject(root, "bms_active_errors", status.bmsActiveErrors);
            cJSON_AddNumberToObject(root, "bms_latched_errors", status.bmsLatchedErrors);
            cJSON_AddNumberToObject(root, "hv_active_errors", status.hvActiveErrors);
            cJSON_AddNumberToObject(root, "hv_latched_errors", status.hvLatchedErrors);
            cJSON_AddNumberToObject(root, "warnings", status.warnings);
            cJSON_AddNumberToObject(root, "uptime_ms", status.uptimeMs);
            cJSON_AddBoolToObject(root, "measurements_fresh", (status.flags & (1U << 3U)) != 0U);
            cJSON_AddBoolToObject(root, "run_request", (status.flags & (1U << 2U)) != 0U);
            cJSON_AddBoolToObject(root, "balancing_enabled", (status.flags & (1U << 5U)) != 0U);
            cJSON_AddBoolToObject(root, "soc_valid", (status.flags & (1U << 6U)) != 0U);
            cJSON_AddBoolToObject(root, "current_sensing_enabled", (status.flags & (1U << 7U)) != 0U);
            if ((status.flags & (1U << 8U)) != 0U) cJSON_AddNumberToObject(root, "soc_last_calibration_unix_s", status.socLastCalibrationUnixS);
        }

        cJSON *bmsStatusJson()
        {
            lockTelemetryState();
            cJSON *root = base("bms_status");
            cJSON *stateJson = cJSON_AddObjectToObject(root, "status");
            addBmsStatus(stateJson);
            unlockTelemetryState();
            return root;
        }

        cJSON *snapshotJson()
        {
            lockTelemetryState();
            if (!completeSnapshotLocked())
            {
                unlockTelemetryState();
                return nullptr;
            }
            cJSON *root = base("snapshot");
            cJSON *stateJson = cJSON_AddObjectToObject(root, "status");
            addBmsStatus(stateJson);
            cJSON *packJson = cJSON_AddObjectToObject(root, "pack");
            cJSON_AddNumberToObject(packJson, "pack_voltage_uV", pack.packVoltageUv);
            cJSON_AddNumberToObject(packJson, "pack_current_raw", pack.packCurrentRaw);
            cJSON_AddNumberToObject(packJson, "soc_raw", pack.socRaw);
            cJSON_AddNumberToObject(packJson, "min_cell_uV", pack.minCellUv);
            cJSON_AddNumberToObject(packJson, "max_cell_uV", pack.maxCellUv);
            cJSON_AddNumberToObject(packJson, "min_ntc_raw", pack.minNtcRaw);
            cJSON_AddNumberToObject(packJson, "max_ntc_raw", pack.maxNtcRaw);
            cJSON_AddNumberToObject(packJson, "min_ic_raw", pack.minIcRaw);
            cJSON_AddNumberToObject(packJson, "max_ic_raw", pack.maxIcRaw);
            if (hasEnergy)
            {
                cJSON *energyJson = cJSON_AddObjectToObject(root, "energy");
                cJSON_AddBoolToObject(energyJson, "valid", energy.valid);
                char chargedText[32]{};
                char dischargedText[32]{};
                std::snprintf(chargedText, sizeof(chargedText), "%llu", static_cast<unsigned long long>(energy.chargedEnergyUWh));
                std::snprintf(dischargedText, sizeof(dischargedText), "%llu", static_cast<unsigned long long>(energy.dischargedEnergyUWh));
                cJSON_AddStringToObject(energyJson, "charged_energy_uWh", chargedText);
                cJSON_AddStringToObject(energyJson, "discharged_energy_uWh", dischargedText);
            }
            if (hasHvVoltages)
            {
                cJSON *hvVoltagesJson = cJSON_AddObjectToObject(root, "hv_voltages");
                cJSON_AddBoolToObject(hvVoltagesJson, "valid", hvVoltages.valid);
                cJSON_AddNumberToObject(hvVoltagesJson, "bat_plus_uV", hvVoltages.batteryVoltageUv);
                cJSON_AddNumberToObject(hvVoltagesJson, "load_plus_uV", hvVoltages.loadVoltageUv);
            }
            cJSON *cellArray = cJSON_AddArrayToObject(root, "cells");
            cJSON *temperatureArray = cJSON_AddArrayToObject(root, "temperatures");
            for (uint8_t slave = 0U; slave < status.slaveCount; ++slave)
            {
                cJSON *cell = cJSON_CreateObject();
                cJSON_AddItemToArray(cellArray, cell);
                cJSON_AddNumberToObject(cell, "slave_index", cells[slave].slaveIndex);
                cJSON_AddNumberToObject(cell, "balance_mask", cells[slave].balanceMask);
                cJSON *values = cJSON_AddArrayToObject(cell, "cell_voltage_uV");
                for (uint32_t value : cells[slave].voltageUv) cJSON_AddItemToArray(values, cJSON_CreateNumber(value));
                cJSON *temperature = cJSON_CreateObject();
                cJSON_AddItemToArray(temperatureArray, temperature);
                cJSON_AddNumberToObject(temperature, "slave_index", temperatures[slave].slaveIndex);
                cJSON *ntcs = cJSON_AddArrayToObject(temperature, "ntc_raw");
                for (uint16_t value : temperatures[slave].ntcRaw) cJSON_AddItemToArray(ntcs, cJSON_CreateNumber(value));
                cJSON_AddNumberToObject(temperature, "ic_temp_raw", temperatures[slave].icRaw);
            }
            unlockTelemetryState();
            return root;
        }

        bool objectHasExactly(cJSON *object, std::initializer_list<const char *> names)
        {
            if (!cJSON_IsObject(object)) return false;
            size_t count = 0U;
            for (cJSON *item = object->child; item != nullptr; item = item->next)
            {
                ++count;
                bool found = false;
                for (const char *name : names)
                {
                    if (item->string != nullptr && std::strcmp(item->string, name) == 0) found = true;
                }
                if (!found) return false;
            }
            return count == names.size();
        }

        bool validRequestId(cJSON *item)
        {
            if (!cJSON_IsString(item) || item->valuestring == nullptr || std::strlen(item->valuestring) > kMaxRequestIdBytes) return false;
            for (const char *p = item->valuestring; *p != '\0'; ++p)
            {
                if (*p < 0x21 || *p > 0x7e) return false;
            }
            return true;
        }

        bool validText(cJSON *item, size_t maximum, bool allowEmpty)
        {
            if (!cJSON_IsString(item) || item->valuestring == nullptr) return false;
            const size_t length = strnlen(item->valuestring, maximum + 1U);
            return (allowEmpty || length > 0U) && length <= maximum;
        }

        bool jsonInteger(cJSON *item, uint32_t maximum, uint32_t &value)
        {
            if (!cJSON_IsNumber(item) || item->valuedouble < 0 || item->valuedouble > maximum ||
                item->valuedouble != static_cast<double>(static_cast<uint32_t>(item->valuedouble))) return false;
            value = static_cast<uint32_t>(item->valuedouble);
            return true;
        }

        bool parseService(cJSON *root, Service &service, std::array<uint8_t, kMaxServiceArgumentBytes> &arguments, uint8_t &argumentLength)
        {
            if (!objectHasExactly(root, {"v", "type", "request_id", "service", "arguments"})) return false;
            cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "v");
            cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
            cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "service");
            cJSON *args = cJSON_GetObjectItemCaseSensitive(root, "arguments");
            if (!cJSON_IsNumber(version) || version->valueint != 1 || !cJSON_IsString(type) || std::strcmp(type->valuestring, "service") != 0 ||
                !validRequestId(cJSON_GetObjectItemCaseSensitive(root, "request_id")) || !cJSON_IsString(name) || !cJSON_IsObject(args)) return false;
            if (std::strcmp(name->valuestring, "set_run_request") == 0)
            {
                cJSON *requested = cJSON_GetObjectItemCaseSensitive(args, "requested");
                if (!objectHasExactly(args, {"requested"}) || !cJSON_IsBool(requested)) return false;
                service = Service::SetRunRequest;
                arguments[0] = cJSON_IsTrue(requested) ? 1U : 0U;
                argumentLength = 1U;
                return true;
            }
            if (std::strcmp(name->valuestring, "set_balancing_enabled") == 0)
            {
                cJSON *enabled = cJSON_GetObjectItemCaseSensitive(args, "enabled");
                if (!objectHasExactly(args, {"enabled"}) || !cJSON_IsBool(enabled)) return false;
                service = Service::SetBalancingEnabled;
                arguments[0] = cJSON_IsTrue(enabled) ? 1U : 0U;
                argumentLength = 1U;
                return true;
            }
            if (std::strcmp(name->valuestring, "acknowledge_faults") == 0)
            {
                if (!objectHasExactly(args, {})) return false;
                service = Service::AcknowledgeFaults;
                argumentLength = 0U;
                return true;
            }
            uint32_t value = 0U;
            if (std::strcmp(name->valuestring, "get_rtc") == 0)
            {
                if (!objectHasExactly(args, {})) return false;
                service = Service::GetRtc;
                argumentLength = 0U;
                return true;
            }
            if (std::strcmp(name->valuestring, "get_device_info") == 0)
            {
                if (!objectHasExactly(args, {})) return false;
                service = Service::GetDeviceInfo;
                argumentLength = 0U;
                return true;
            }
            if (std::strcmp(name->valuestring, "get_config") == 0)
            {
                if (!objectHasExactly(args, {})) return false;
                service = Service::GetConfig;
                argumentLength = 0U;
                return true;
            }
            if (std::strcmp(name->valuestring, "get_diagnostic_report") == 0)
            {
                uint32_t slaveIndex = 0U;
                if (!objectHasExactly(args, {"slave_index"}) ||
                    !jsonInteger(cJSON_GetObjectItemCaseSensitive(args, "slave_index"), 31U, slaveIndex)) return false;
                service = Service::GetDiagnosticReport;
                arguments[0] = static_cast<uint8_t>(slaveIndex);
                argumentLength = 1U;
                return true;
            }
            if (std::strcmp(name->valuestring, "set_config") == 0)
            {
                uint32_t slaveCount = 0U;
                uint32_t currentSenseSlave = 0U;
                uint32_t shuntResistance = 0U;
                uint32_t batteryCapacity = 0U;
                cJSON *invertCurrent = cJSON_GetObjectItemCaseSensitive(args, "invert_current");
                if (!objectHasExactly(args, {"slave_count", "current_sense_slave", "shunt_resistance_uohm", "battery_capacity_mah", "invert_current", "balance_enabled", "startup_diagnostics"}) ||
                    !jsonInteger(cJSON_GetObjectItemCaseSensitive(args, "slave_count"), 32U, slaveCount) ||
                    !jsonInteger(cJSON_GetObjectItemCaseSensitive(args, "current_sense_slave"), 32U, currentSenseSlave) ||
                    !jsonInteger(cJSON_GetObjectItemCaseSensitive(args, "shunt_resistance_uohm"), UINT32_MAX, shuntResistance) ||
                    !jsonInteger(cJSON_GetObjectItemCaseSensitive(args, "battery_capacity_mah"), UINT32_MAX, batteryCapacity) ||
                    !cJSON_IsBool(invertCurrent)) return false;
                service = Service::SetConfig;
                arguments[0] = static_cast<uint8_t>(slaveCount);
                arguments[1] = static_cast<uint8_t>(currentSenseSlave);
                arguments[2] = static_cast<uint8_t>(shuntResistance);
                arguments[3] = static_cast<uint8_t>(shuntResistance >> 8U);
                arguments[4] = static_cast<uint8_t>(shuntResistance >> 16U);
                arguments[5] = static_cast<uint8_t>(shuntResistance >> 24U);
                arguments[6] = static_cast<uint8_t>(batteryCapacity);
                arguments[7] = static_cast<uint8_t>(batteryCapacity >> 8U);
                arguments[8] = static_cast<uint8_t>(batteryCapacity >> 16U);
                arguments[9] = static_cast<uint8_t>(batteryCapacity >> 24U);
                arguments[10] = cJSON_IsTrue(invertCurrent) ? 1U : 0U;
                cJSON *balanceEnabled = cJSON_GetObjectItemCaseSensitive(args, "balance_enabled");
                if (!cJSON_IsBool(balanceEnabled)) return false;
                arguments[11] = cJSON_IsTrue(balanceEnabled) ? 1U : 0U;
                cJSON *startupDiagnostics = cJSON_GetObjectItemCaseSensitive(args, "startup_diagnostics");
                if (!cJSON_IsBool(startupDiagnostics)) return false;
                arguments[12] = cJSON_IsTrue(startupDiagnostics) ? 1U : 0U;
                argumentLength = 13U;
                return true;
            }
            if (std::strcmp(name->valuestring, "read_register") == 0)
            {
                uint32_t slave = 0U;
                if (!objectHasExactly(args, {"slave_index", "register"}) ||
                    !jsonInteger(cJSON_GetObjectItemCaseSensitive(args, "slave_index"), 255U, slave) ||
                    !jsonInteger(cJSON_GetObjectItemCaseSensitive(args, "register"), 255U, value)) return false;
                service = Service::ReadRegister;
                arguments[0] = static_cast<uint8_t>(slave);
                arguments[1] = static_cast<uint8_t>(value);
                argumentLength = 2U;
                return true;
            }
            return false;
        }

        bool parseWifiConfigure(cJSON *root, const char *&ssid, const char *&password)
        {
            if (!objectHasExactly(root, {"v", "type", "request_id", "ssid", "password"})) return false;
            cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "v");
            cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
            cJSON *ssidItem = cJSON_GetObjectItemCaseSensitive(root, "ssid");
            cJSON *passwordItem = cJSON_GetObjectItemCaseSensitive(root, "password");
            if (!cJSON_IsNumber(version) || version->valueint != 1 || !cJSON_IsString(type) || std::strcmp(type->valuestring, "wifi_configure") != 0 ||
                !validRequestId(cJSON_GetObjectItemCaseSensitive(root, "request_id")) || !validText(ssidItem, 32U, false) || !validText(passwordItem, 63U, true)) return false;
            ssid = ssidItem->valuestring;
            password = passwordItem->valuestring;
            return true;
        }

        bool parseWifiScan(cJSON *root)
        {
            if (!objectHasExactly(root, {"v", "type", "request_id"})) return false;
            cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "v");
            cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
            return cJSON_IsNumber(version) && version->valueint == 1 && cJSON_IsString(type) && std::strcmp(type->valuestring, "wifi_scan") == 0 &&
                   validRequestId(cJSON_GetObjectItemCaseSensitive(root, "request_id"));
        }

        bool parseMqttConfigure(cJSON *root, const char *&host, uint16_t &port, const char *&username, const char *&password)
        {
            if (!objectHasExactly(root, {"v", "type", "request_id", "host", "port", "username", "password"})) return false;
            cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "v");
            cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
            cJSON *hostItem = cJSON_GetObjectItemCaseSensitive(root, "host");
            cJSON *usernameItem = cJSON_GetObjectItemCaseSensitive(root, "username");
            cJSON *passwordItem = cJSON_GetObjectItemCaseSensitive(root, "password");
            uint32_t portValue = 0U;
            if (!cJSON_IsNumber(version) || version->valueint != 1 || !cJSON_IsString(type) || std::strcmp(type->valuestring, "mqtt_configure") != 0 ||
                !validRequestId(cJSON_GetObjectItemCaseSensitive(root, "request_id")) || !validText(hostItem, 63U, false) || !validText(usernameItem, 63U, false) ||
                !validText(passwordItem, 63U, false) || !jsonInteger(cJSON_GetObjectItemCaseSensitive(root, "port"), 65535U, portValue) || portValue == 0U) return false;
            host = hostItem->valuestring;
            username = usernameItem->valuestring;
            password = passwordItem->valuestring;
            port = static_cast<uint16_t>(portValue);
            return true;
        }

        void sendServiceResult(int socket, const char *requestId, Service service, ServiceResult result)
        {
            cJSON *root = base("service_result");
            cJSON_AddStringToObject(root, "request_id", requestId);
            cJSON_AddStringToObject(root, "service", serviceName(service));
            cJSON_AddStringToObject(root, "result", resultName(result));
            (void)sendJsonTo(socket, root);
        }

        void sendWifiConfigurationResult(int socket, const char *requestId, const char *result)
        {
            cJSON *root = base("wifi_configuration_result");
            cJSON_AddStringToObject(root, "request_id", requestId);
            cJSON_AddStringToObject(root, "result", result);
            (void)sendJsonTo(socket, root);
        }

        void sendMqttConfigurationResult(int socket, const char *requestId, const char *result)
        {
            cJSON *root = base("mqtt_configuration_result");
            cJSON_AddStringToObject(root, "request_id", requestId);
            cJSON_AddStringToObject(root, "result", result);
            (void)sendJsonTo(socket, root);
        }

        void sendWifiScanResult(int socket, const char *requestId, const char *result, const Wifi::ScanResults *results = nullptr)
        {
            cJSON *root = base("wifi_scan_result");
            cJSON_AddStringToObject(root, "request_id", requestId);
            cJSON_AddStringToObject(root, "result", result);
            if (results != nullptr)
            {
                cJSON *networks = cJSON_AddArrayToObject(root, "networks");
                for (size_t index = 0U; index < results->count; ++index)
                {
                    cJSON *network = cJSON_CreateObject();
                    cJSON_AddItemToArray(networks, network);
                    cJSON_AddStringToObject(network, "ssid", results->networks[index].ssid.data());
                    cJSON_AddNumberToObject(network, "rssi", results->networks[index].rssi);
                    cJSON_AddBoolToObject(network, "secure", results->networks[index].secure);
                }
            }
            (void)sendJsonTo(socket, root);
        }

        esp_err_t assetHandler(httpd_req_t *request)
        {
            const char *path = std::strcmp(request->uri, "/") == 0 ? "index.html" : request->uri + 1U;
            for (size_t index = 0U; index < GatewayAssets::kAssetCount; ++index)
            {
                if (std::strcmp(path, GatewayAssets::kAssets[index].path) == 0)
                {
                    httpd_resp_set_type(request, GatewayAssets::kAssets[index].mime);
                    // The HTML shell names versioned JavaScript and CSS files.
                    // It must never remain cached across a Gateway firmware
                    // swap, while those content-addressed assets are safe to
                    // cache indefinitely.
                    httpd_resp_set_hdr(request, "Cache-Control", std::strcmp(path, "index.html") == 0 ? "no-store" : "public, max-age=31536000, immutable");
                    return httpd_resp_send(request, reinterpret_cast<const char *>(GatewayAssets::kAssets[index].data), GatewayAssets::kAssets[index].bytes);
                }
            }
            if (Wifi::isAccessPointActive())
            {
                httpd_resp_set_status(request, "302 Found");
                httpd_resp_set_hdr(request, "Location", "/");
                return httpd_resp_sendstr(request, "FlexBMS Wi-Fi setup");
            }
            return httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "Not found");
        }

        esp_err_t logHandler(httpd_req_t *request)
        {
            return httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "Diagnostic log unavailable");
        }

        bool validVersion(const char *value)
        {
            if (value == nullptr || value[0] == '\0' || std::strlen(value) >= 48U) return false;
            for (const char *cursor = value; *cursor != '\0'; ++cursor)
            {
                if (!(std::isdigit(static_cast<unsigned char>(*cursor)) || *cursor == '.' || *cursor == '-' || *cursor == '+')) return false;
            }
            return true;
        }

        bool readHeader(httpd_req_t *request, const char *name, char *value, size_t capacity)
        {
            const size_t length = httpd_req_get_hdr_value_len(request, name);
            return length != 0U && length < capacity && httpd_req_get_hdr_value_str(request, name, value, capacity) == ESP_OK;
        }

        bool parseUint32(const char *text, uint32_t &value, int base)
        {
            if (text == nullptr || text[0] == '\0') return false;
            char *end = nullptr;
            const unsigned long parsed = std::strtoul(text, &end, base);
            if (end == nullptr || *end != '\0' || parsed > 0xFFFFFFFFUL) return false;
            value = static_cast<uint32_t>(parsed);
            return true;
        }

        bool validCrc32(const char *text)
        {
            if (text == nullptr || std::strlen(text) != 8U) return false;
            for (const char *cursor = text; *cursor != '\0'; ++cursor)
            {
                if (!std::isxdigit(static_cast<unsigned char>(*cursor))) return false;
            }
            return true;
        }

        esp_err_t firmwareHandler(httpd_req_t *request)
        {
            const FirmwareUpdate::Target target = std::strcmp(request->uri, "/api/firmware/gateway") == 0 ? FirmwareUpdate::Target::Gateway : FirmwareUpdate::Target::Stm32;
            const bool gatewayRecovery = target == FirmwareUpdate::Target::Gateway && Wifi::isAccessPointActive();
            const bool stationLan = Wifi::allowsBmsServices() && !Wifi::isAccessPointActive();
            if (!gatewayRecovery && !stationLan)
            {
                return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, target == FirmwareUpdate::Target::Gateway
                                                                              ? "Gateway firmware update requires Wi-Fi or the setup AP"
                                                                              : "STM32 firmware update is available only on the station LAN");
            }
            if (!FirmwareUpdate::isAvailable(target) || serviceBusy())
            {
                const bool stm32NeedsRecovery = target == FirmwareUpdate::Target::Stm32 && FirmwareUpdate::isAvailable() && !FirmwareUpdate::isAvailable(target);
                return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN,
                                           stm32NeedsRecovery ? "STM32 wired recovery is required before another update" : "Firmware update already in progress");
            }
            char manifestTarget[16]{};
            char version[48]{};
            char lengthText[16]{};
            char crcText[16]{};
            if (!readHeader(request, "X-FlexBMS-Target", manifestTarget, sizeof(manifestTarget)) ||
                !readHeader(request, "X-FlexBMS-Version", version, sizeof(version)) ||
                !readHeader(request, "X-FlexBMS-Length", lengthText, sizeof(lengthText)) ||
                !readHeader(request, "X-FlexBMS-CRC32", crcText, sizeof(crcText)))
            {
                return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Missing firmware manifest headers");
            }
            const char *expectedTarget = updateTargetName(target);
            uint32_t imageBytes = 0U;
            uint32_t imageCrc = 0U;
            if (std::strcmp(manifestTarget, expectedTarget) != 0 || !validVersion(version) || !parseUint32(lengthText, imageBytes, 10) ||
                !validCrc32(crcText) || !parseUint32(crcText, imageCrc, 16) || request->content_len <= 0 || static_cast<uint32_t>(request->content_len) != imageBytes ||
                !FirmwareUpdate::beginUpload(target, version, imageBytes, imageCrc))
            {
                return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid firmware manifest or image size");
            }
            std::array<uint8_t, 1024U> chunk{};
            int remaining = request->content_len;
            uint8_t receiveTimeouts = 0U;
            while (remaining > 0)
            {
                const int received = httpd_req_recv(request, reinterpret_cast<char *>(chunk.data()), std::min<int>(remaining, static_cast<int>(chunk.size())));
                if (received == HTTPD_SOCK_ERR_TIMEOUT)
                {
                    if (++receiveTimeouts < kFirmwareReceiveTimeoutLimit) continue;
                    FirmwareUpdate::abortUpload("Browser upload timed out");
                    return httpd_resp_send_err(request, HTTPD_408_REQ_TIMEOUT, "Firmware upload timed out");
                }
                if (received <= 0 || !FirmwareUpdate::writeUpload(chunk.data(), static_cast<size_t>(received)))
                {
                    FirmwareUpdate::abortUpload("Browser upload failed");
                    return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Firmware upload failed");
                }
                receiveTimeouts = 0U;
                remaining -= received;
            }
            if (!FirmwareUpdate::finishUpload()) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Firmware image validation failed");
            httpd_resp_set_type(request, "application/json");
            return httpd_resp_sendstr(request, "{\"result\":\"accepted\"}");
        }

        esp_err_t wsHandler(httpd_req_t *request)
        {
            const int socket = httpd_req_to_sockfd(request);
            if (request->method == HTTP_GET)
            {
                prepareWebSocketConnection(socket);
                (void)sendJsonTo(socket, hello());
                if (telemetryHasStatus()) (void)sendJsonTo(socket, bmsStatusJson());
                if (cJSON *snapshot = snapshotJson()) (void)sendJsonTo(socket, snapshot);
                return ESP_OK;
            }
            httpd_ws_frame_t frame{};
            if (httpd_ws_recv_frame(request, &frame, 0U) != ESP_OK || frame.type != HTTPD_WS_TYPE_TEXT || frame.len == 0U || frame.len > kMaxBrowserMessageBytes) return ESP_FAIL;
            // httpd runs with a 4 KiB task stack. Do not reserve the maximum
            // browser frame there: the function frame exists even for the
            // WebSocket handshake, before any browser payload is received.
            std::unique_ptr<uint8_t[]> text(new (std::nothrow) uint8_t[frame.len]);
            if (text == nullptr) return ESP_ERR_NO_MEM;
            frame.payload = text.get();
            if (httpd_ws_recv_frame(request, &frame, frame.len) != ESP_OK) return ESP_FAIL;
            cJSON *root = cJSON_ParseWithLength(reinterpret_cast<const char *>(text.get()), frame.len);
            if (root == nullptr) return ESP_OK;
            cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
            const bool isService = cJSON_IsString(type) && std::strcmp(type->valuestring, "service") == 0;
            const bool isConfigure = cJSON_IsString(type) && std::strcmp(type->valuestring, "wifi_configure") == 0;
            const bool isScan = cJSON_IsString(type) && std::strcmp(type->valuestring, "wifi_scan") == 0;
            const bool isMqttConfigure = cJSON_IsString(type) && std::strcmp(type->valuestring, "mqtt_configure") == 0;
            if (isService)
            {
                Service requestedService{};
                std::array<uint8_t, kMaxServiceArgumentBytes> arguments{};
                uint8_t argumentLength = 0U;
                if (parseService(root, requestedService, arguments, argumentLength))
                {
                    const char *requestId = cJSON_GetObjectItemCaseSensitive(root, "request_id")->valuestring;
                    if (!Wifi::allowsBmsServices() || FirmwareUpdate::getStatus().phase == FirmwareUpdate::Phase::Uploading || FirmwareUpdate::getStatus().phase == FirmwareUpdate::Phase::Installing)
                    {
                        sendServiceResult(socket, requestId, requestedService, ServiceResult::Denied);
                    }
                    else
                    {
                        bool accepted = false;
                        bool healthy = false;
                        ServiceSender sender = nullptr;
                        uint8_t sequence = 0U;
                        lockServiceState();
                        healthy = uartHealthy;
                        if (serviceInFlight)
                        {
                            unlockServiceState();
                            sendServiceResult(socket, requestId, requestedService, ServiceResult::Busy);
                        }
                        else if (!healthy || serviceSender == nullptr)
                        {
                            unlockServiceState();
                            sendServiceResult(socket, requestId, requestedService, ServiceResult::TransportError);
                        }
                        else
                        {
                            sequence = nextServiceSequence++;
                            if (nextServiceSequence == 0U) nextServiceSequence = 1U;
                            std::strncpy(pendingRequestId, requestId, kMaxRequestIdBytes);
                            pendingRequestId[kMaxRequestIdBytes] = '\0';
                            pendingService = requestedService;
                            pendingSequence = sequence;
                            pendingServiceSocket = socket;
                            pendingServiceStartedUs = esp_timer_get_time();
                            serviceInFlight = true;
                            sender = serviceSender;
                            accepted = true;
                            unlockServiceState();
                        }
                        if (accepted && !sender(requestedService, arguments.data(), argumentLength, sequence))
                        {
                            bool clearedForWriteFailure = false;
                            lockServiceState();
                            if (serviceInFlight && pendingSequence == sequence)
                            {
                                serviceInFlight = false;
                                pendingServiceSocket = -1;
                                internalServiceCompletion = nullptr;
                                clearedForWriteFailure = true;
                            }
                            unlockServiceState();
                            if (clearedForWriteFailure)
                            {
                                ESP_LOGW(kLogTag, "Service %s could not be written to STM32 UART", serviceName(requestedService));
                                sendServiceResult(socket, requestId, requestedService, ServiceResult::TransportError);
                            }
                        }
                    }
                }
            }
            else if (isConfigure)
            {
                const char *ssid = nullptr;
                const char *password = nullptr;
                if (parseWifiConfigure(root, ssid, password))
                {
                    const char *requestId = cJSON_GetObjectItemCaseSensitive(root, "request_id")->valuestring;
                    sendWifiConfigurationResult(socket, requestId, Wifi::configure(ssid, password) ? "accepted" : "error");
                }
            }
            else if (isScan && parseWifiScan(root))
            {
                const char *requestId = cJSON_GetObjectItemCaseSensitive(root, "request_id")->valuestring;
                switch (Wifi::requestScan())
                {
                case Wifi::ScanRequestResult::Started:
                    pendingScanSocket = socket;
                    std::strncpy(pendingScanRequestId, requestId, kMaxRequestIdBytes);
                    pendingScanRequestId[kMaxRequestIdBytes] = '\0';
                    break;
                case Wifi::ScanRequestResult::Busy: sendWifiScanResult(socket, requestId, "busy"); break;
                case Wifi::ScanRequestResult::RateLimited: sendWifiScanResult(socket, requestId, "rate_limited"); break;
                default: sendWifiScanResult(socket, requestId, "unavailable"); break;
                }
            }
            else if (isMqttConfigure)
            {
                const char *host = nullptr;
                const char *username = nullptr;
                const char *password = nullptr;
                uint16_t port = 0U;
                if (parseMqttConfigure(root, host, port, username, password))
                {
                    const char *requestId = cJSON_GetObjectItemCaseSensitive(root, "request_id")->valuestring;
                    const bool allowed = Wifi::allowsBmsServices() && !Wifi::isAccessPointActive();
                    sendMqttConfigurationResult(socket, requestId, allowed && Mqtt::configure(host, port, username, password) ? "accepted" : "error");
                    publishGatewayStatus();
                }
            }
            cJSON_Delete(root);
            return ESP_OK;
        }
    }

    bool start(ServiceSender sender)
    {
        if (server != nullptr) return true;
        webSocketDeliveryMutex = xSemaphoreCreateMutex();
        serviceStateMutex = xSemaphoreCreateMutex();
        telemetryStateMutex = xSemaphoreCreateMutex();
        if (webSocketDeliveryMutex == nullptr || serviceStateMutex == nullptr || telemetryStateMutex == nullptr)
        {
            if (telemetryStateMutex != nullptr) vSemaphoreDelete(telemetryStateMutex);
            if (serviceStateMutex != nullptr) vSemaphoreDelete(serviceStateMutex);
            if (webSocketDeliveryMutex != nullptr) vSemaphoreDelete(webSocketDeliveryMutex);
            serviceStateMutex = nullptr;
            telemetryStateMutex = nullptr;
            webSocketDeliveryMutex = nullptr;
            return false;
        }
        serviceSender = sender;
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.lru_purge_enable = true;
        // The ESP32-C3 has ten lwIP sockets total. Keep enough capacity for
        // MQTT, DNS/NTP, and an OTA upload instead of allowing seven browser
        // clients to consume the network stack.
        config.max_open_sockets = kMaxHttpClients;
        config.max_uri_handlers = 6;
        config.uri_match_fn = httpd_uri_match_wildcard;
        if (httpd_start(&server, &config) != ESP_OK)
        {
            vSemaphoreDelete(webSocketDeliveryMutex);
            vSemaphoreDelete(serviceStateMutex);
            vSemaphoreDelete(telemetryStateMutex);
            webSocketDeliveryMutex = nullptr;
            serviceStateMutex = nullptr;
            telemetryStateMutex = nullptr;
            return false;
        }
        const httpd_uri_t log = {.uri = "/api/diagnostic-log", .method = HTTP_GET, .handler = logHandler, .user_ctx = nullptr,
                                 .is_websocket = false, .handle_ws_control_frames = false, .supported_subprotocol = nullptr};
        const httpd_uri_t ws = {.uri = "/ws", .method = HTTP_GET, .handler = wsHandler, .user_ctx = nullptr,
                                 .is_websocket = true, .handle_ws_control_frames = false, .supported_subprotocol = nullptr};
        const httpd_uri_t gatewayFirmware = {.uri = "/api/firmware/gateway", .method = HTTP_POST, .handler = firmwareHandler, .user_ctx = nullptr,
                                             .is_websocket = false, .handle_ws_control_frames = false, .supported_subprotocol = nullptr};
        const httpd_uri_t stm32Firmware = {.uri = "/api/firmware/stm32", .method = HTTP_POST, .handler = firmwareHandler, .user_ctx = nullptr,
                                           .is_websocket = false, .handle_ws_control_frames = false, .supported_subprotocol = nullptr};
        const httpd_uri_t assets = {.uri = "/*", .method = HTTP_GET, .handler = assetHandler, .user_ctx = nullptr,
                                    .is_websocket = false, .handle_ws_control_frames = false, .supported_subprotocol = nullptr};
        return httpd_register_uri_handler(server, &log) == ESP_OK && httpd_register_uri_handler(server, &ws) == ESP_OK &&
               httpd_register_uri_handler(server, &gatewayFirmware) == ESP_OK && httpd_register_uri_handler(server, &stm32Firmware) == ESP_OK &&
               httpd_register_uri_handler(server, &assets) == ESP_OK;
    }

    void poll()
    {
        retireUnresponsiveWebSockets();
        flushQueuedWebSockets();
        Wifi::ScanResults results{};
        if (Wifi::consumeScanResults(results) && pendingScanSocket >= 0)
        {
            sendWifiScanResult(pendingScanSocket, pendingScanRequestId, results.successful ? "ok" : "unavailable", results.successful ? &results : nullptr);
            pendingScanSocket = -1;
            pendingScanRequestId[0] = '\0';
        }
        bool serviceTimedOut = false;
        InternalServiceCompletion completion = nullptr;
        int socket = -1;
        Service service = Service::SetRunRequest;
        char requestId[kMaxRequestIdBytes + 1U] = {};
        lockServiceState();
        if (serviceInFlight && esp_timer_get_time() - pendingServiceStartedUs >= 3000000LL)
        {
            serviceTimedOut = true;
            completion = internalServiceCompletion;
            socket = pendingServiceSocket;
            service = pendingService;
            std::strncpy(requestId, pendingRequestId, kMaxRequestIdBytes);
            internalServiceCompletion = nullptr;
            serviceInFlight = false;
            pendingServiceSocket = -1;
        }
        unlockServiceState();
        if (serviceTimedOut)
        {
            ESP_LOGW(kLogTag, "Service %s timed out waiting for STM32 response", serviceName(service));
            if (completion != nullptr)
            {
                completion(ServiceResult::TransportError);
                publishGatewayStatus();
            }
            else
            {
                sendServiceResult(socket, requestId, service, ServiceResult::TransportError);
            }
        }
    }

    void setUartHealthy(bool healthy)
    {
        bool changed = false;
        lockServiceState();
        if (uartHealthy != healthy)
        {
            uartHealthy = healthy;
            changed = true;
        }
        unlockServiceState();
        if (changed) publishGatewayStatus();
    }

    void publishGatewayStatus()
    {
        const bool currentServiceCapability = Wifi::allowsBmsServices();
        lockServiceState();
        const bool capabilityChanged = !serviceCapabilityKnown || serviceCapability != currentServiceCapability;
        if (capabilityChanged)
        {
            serviceCapabilityKnown = true;
            serviceCapability = currentServiceCapability;
        }
        unlockServiceState();
        if (capabilityChanged) broadcast(hello());
        cJSON *root = base("gateway_status");
        addGatewayStatus(root);
        broadcast(root);
    }

    void publishFrame(const UartV1::Frame &frame, uint32_t gatewayUptimeMs)
    {
        bool statusPublished = false;
        cJSON *eventToPublish = nullptr;
        lockTelemetryState();
        if (frame.type == UartV1::MessageType::Status && UartV1::decodeStatus(frame, status))
        {
            hasStatus = true;
            statusPublished = true;
            if ((status.flags & (1U << 3U)) == 0U)
            {
                hasPack = false;
                hasEnergy = false;
                hasHvVoltages = false;
                hasCell.fill(false);
                hasTemperature.fill(false);
            }
        }
        else if (frame.type == UartV1::MessageType::Pack && UartV1::decodePack(frame, pack)) hasPack = true;
        else if (frame.type == UartV1::MessageType::Energy && UartV1::decodeEnergy(frame, energy)) hasEnergy = true;
        else if (frame.type == UartV1::MessageType::HvVoltages && UartV1::decodeHvVoltages(frame, hvVoltages)) hasHvVoltages = true;
        else if (frame.type == UartV1::MessageType::Cell)
        {
            UartV1::Cell cell{};
            if (UartV1::decodeCell(frame, cell) && cell.slaveIndex < kMaxSlaves)
            {
                cells[cell.slaveIndex] = cell;
                hasCell[cell.slaveIndex] = true;
            }
        }
        else if (frame.type == UartV1::MessageType::Temperature)
        {
            UartV1::Temperature temperature{};
            if (UartV1::decodeTemperature(frame, temperature) && temperature.slaveIndex < kMaxSlaves)
            {
                temperatures[temperature.slaveIndex] = temperature;
                hasTemperature[temperature.slaveIndex] = true;
            }
        }
        else if (frame.type == UartV1::MessageType::Event && frame.length == 5U)
        {
            cJSON *event = base("event");
            cJSON_AddNumberToObject(event, "event_id", frame.payload[0]);
            const uint32_t value = static_cast<uint32_t>(frame.payload[1]) |
                                   (static_cast<uint32_t>(frame.payload[2]) << 8U) |
                                   (static_cast<uint32_t>(frame.payload[3]) << 16U) |
                                   (static_cast<uint32_t>(frame.payload[4]) << 24U);
            cJSON_AddNumberToObject(event, "value", value);
            cJSON_AddNumberToObject(event, "gateway_uptime_ms", gatewayUptimeMs);
            eventToPublish = event;
        }
        unlockTelemetryState();
        if (statusPublished) broadcast(bmsStatusJson());
        if (eventToPublish != nullptr) broadcast(eventToPublish);
        if (cJSON *snapshot = snapshotJson()) broadcast(snapshot);
    }

    void completeService(uint8_t sequence, ServiceResult result, const uint8_t *data, uint8_t dataLength)
    {
        Service service = Service::SetRunRequest;
        int socket = -1;
        char requestId[kMaxRequestIdBytes + 1U] = {};
        std::array<uint8_t, 64U> responseData{};
        const uint8_t copiedDataLength = std::min<uint8_t>(dataLength, static_cast<uint8_t>(responseData.size()));
        if (data != nullptr && copiedDataLength != 0U)
        {
            std::memcpy(responseData.data(), data, copiedDataLength);
        }

        lockServiceState();
        if (!serviceInFlight || sequence != pendingSequence)
        {
            unlockServiceState();
            return;
        }
        const InternalServiceCompletion completion = internalServiceCompletion;
        service = pendingService;
        socket = pendingServiceSocket;
        std::strncpy(requestId, pendingRequestId, kMaxRequestIdBytes);
        internalServiceCompletion = nullptr;
        serviceInFlight = false;
        pendingServiceSocket = -1;
        unlockServiceState();

        if (completion != nullptr)
        {
            completion(result);
            publishGatewayStatus();
            return;
        }
        cJSON *root = base("service_result");
        cJSON_AddStringToObject(root, "request_id", requestId);
        cJSON_AddStringToObject(root, "service", serviceName(service));
        cJSON_AddStringToObject(root, "result", resultName(result));
        if (result == ServiceResult::Ok && service == Service::ReadRegister && copiedDataLength == 4U)
        {
            cJSON *value = cJSON_AddObjectToObject(root, "data");
            cJSON_AddNumberToObject(value, "slave_index", responseData[0]);
            cJSON_AddNumberToObject(value, "register", responseData[1]);
            cJSON_AddNumberToObject(value, "value", static_cast<uint16_t>(responseData[2]) | (static_cast<uint16_t>(responseData[3]) << 8U));
        }
        else if (result == ServiceResult::Ok && service == Service::GetRtc && copiedDataLength == 4U)
        {
            cJSON *value = cJSON_AddObjectToObject(root, "data");
            cJSON_AddNumberToObject(value, "unix_time_s", static_cast<uint32_t>(responseData[0]) |
                                                            (static_cast<uint32_t>(responseData[1]) << 8U) |
                                                            (static_cast<uint32_t>(responseData[2]) << 16U) |
                                                            (static_cast<uint32_t>(responseData[3]) << 24U));
        }
        else if (result == ServiceResult::Ok && service == Service::GetDeviceInfo && copiedDataLength == 4U)
        {
            cJSON *value = cJSON_AddObjectToObject(root, "data");
            cJSON_AddNumberToObject(value, "firmware_version_packed", static_cast<uint32_t>(responseData[0]) |
                                                                     (static_cast<uint32_t>(responseData[1]) << 8U) |
                                                                     (static_cast<uint32_t>(responseData[2]) << 16U) |
                                                                     (static_cast<uint32_t>(responseData[3]) << 24U));
        }
        else if (result == ServiceResult::Ok && service == Service::GetConfig && copiedDataLength == 18U)
        {
            cJSON *value = cJSON_AddObjectToObject(root, "data");
            const char *reason = responseData[0] == 0U ? "valid" : responseData[0] == 1U ? "blank" : responseData[0] == 2U ? "version_mismatch" : "corrupt";
            cJSON_AddStringToObject(value, "reason", reason);
            cJSON_AddNumberToObject(value, "expected_version", static_cast<uint16_t>(responseData[1]) | (static_cast<uint16_t>(responseData[2]) << 8U));
            cJSON_AddNumberToObject(value, "stored_version", static_cast<uint16_t>(responseData[3]) | (static_cast<uint16_t>(responseData[4]) << 8U));
            cJSON_AddNumberToObject(value, "slave_count", responseData[5]);
            cJSON_AddNumberToObject(value, "current_sense_slave", responseData[6]);
            cJSON_AddNumberToObject(value, "shunt_resistance_uohm", static_cast<uint32_t>(responseData[7]) | (static_cast<uint32_t>(responseData[8]) << 8U) |
                                                                            (static_cast<uint32_t>(responseData[9]) << 16U) | (static_cast<uint32_t>(responseData[10]) << 24U));
            cJSON_AddNumberToObject(value, "battery_capacity_mah", static_cast<uint32_t>(responseData[11]) | (static_cast<uint32_t>(responseData[12]) << 8U) |
                                                                      (static_cast<uint32_t>(responseData[13]) << 16U) | (static_cast<uint32_t>(responseData[14]) << 24U));
            cJSON_AddBoolToObject(value, "invert_current", responseData[15] != 0U);
            cJSON_AddBoolToObject(value, "balance_enabled", responseData[16] != 0U);
            cJSON_AddBoolToObject(value, "startup_diagnostics", responseData[17] != 0U);
        }
        else if (result == ServiceResult::Ok && service == Service::GetDiagnosticReport && copiedDataLength == 6U)
        {
            cJSON *value = cJSON_AddObjectToObject(root, "data");
            cJSON_AddNumberToObject(value, "slave_index", responseData[0]);
            cJSON_AddNumberToObject(value, "cid", responseData[1]);
            cJSON_AddNumberToObject(value, "failed_checks", static_cast<uint16_t>(responseData[2]) | (static_cast<uint16_t>(responseData[3]) << 8U));
            cJSON_AddNumberToObject(value, "status_code", responseData[4]);
            cJSON_AddNumberToObject(value, "failed_diagnostic", responseData[5]);
        }
        (void)sendJsonTo(socket, root);
    }

    bool serviceBusy()
    {
        lockServiceState();
        const bool busy = serviceInFlight;
        unlockServiceState();
        return busy;
    }

    bool beginInternalService(Service service, const uint8_t *arguments, uint8_t argumentLength, InternalServiceCompletion completion)
    {
        if (arguments == nullptr || completion == nullptr || argumentLength > kMaxServiceArgumentBytes || !Wifi::allowsBmsServices() ||
            FirmwareUpdate::getStatus().phase == FirmwareUpdate::Phase::Uploading ||
            FirmwareUpdate::getStatus().phase == FirmwareUpdate::Phase::Installing)
        {
            return false;
        }
        lockServiceState();
        if (serviceInFlight || !uartHealthy || serviceSender == nullptr)
        {
            unlockServiceState();
            return false;
        }
        const ServiceSender sender = serviceSender;
        const uint8_t sequence = nextServiceSequence++;
        if (nextServiceSequence == 0U) nextServiceSequence = 1U;
        pendingService = service;
        pendingSequence = sequence;
        pendingServiceSocket = -1;
        pendingServiceStartedUs = esp_timer_get_time();
        internalServiceCompletion = completion;
        serviceInFlight = true;
        unlockServiceState();
        if (sender(service, arguments, argumentLength, sequence)) return true;
        bool clearedForWriteFailure = false;
        lockServiceState();
        if (serviceInFlight && pendingSequence == sequence)
        {
            serviceInFlight = false;
            internalServiceCompletion = nullptr;
            clearedForWriteFailure = true;
        }
        unlockServiceState();
        if (clearedForWriteFailure)
        {
            ESP_LOGW(kLogTag, "Internal service %s could not be written to STM32 UART", serviceName(service));
            return false;
        }
        // A response completed the request while the sender was returning a
        // failure. Treat the completed request as accepted and do not create
        // a second transport result.
        return true;
    }

    bool verifyBrowserApi()
    {
        cJSON *bad = cJSON_Parse("{\"v\":1,\"type\":\"service\",\"request_id\":\"x\",\"service\":\"set_run_request\",\"arguments\":{\"requested\":true},\"raw\":\"FB\"}");
        Service service{};
        std::array<uint8_t, kMaxServiceArgumentBytes> arguments{};
        uint8_t length = 0U;
        const bool serviceRejected = !parseService(bad, service, arguments, length);
        cJSON_Delete(bad);
        cJSON *getRtc = cJSON_Parse("{\"v\":1,\"type\":\"service\",\"request_id\":\"x\",\"service\":\"get_rtc\",\"arguments\":{}}");
        const bool getRtcAccepted = parseService(getRtc, service, arguments, length) && service == Service::GetRtc && length == 0U;
        cJSON_Delete(getRtc);
        cJSON *deviceInfo = cJSON_Parse("{\"v\":1,\"type\":\"service\",\"request_id\":\"x\",\"service\":\"get_device_info\",\"arguments\":{}}");
        const bool deviceInfoAccepted = parseService(deviceInfo, service, arguments, length) && service == Service::GetDeviceInfo && length == 0U;
        cJSON_Delete(deviceInfo);
        cJSON *getConfig = cJSON_Parse("{\"v\":1,\"type\":\"service\",\"request_id\":\"x\",\"service\":\"get_config\",\"arguments\":{}}" );
        const bool getConfigAccepted = parseService(getConfig, service, arguments, length) && service == Service::GetConfig && length == 0U;
        cJSON_Delete(getConfig);
        cJSON *getDiagnosticReport = cJSON_Parse("{\"v\":1,\"type\":\"service\",\"request_id\":\"x\",\"service\":\"get_diagnostic_report\",\"arguments\":{\"slave_index\":0}}");
        const bool getDiagnosticReportAccepted = parseService(getDiagnosticReport, service, arguments, length) && service == Service::GetDiagnosticReport && length == 1U && arguments[0] == 0U;
        cJSON_Delete(getDiagnosticReport);
        cJSON *setConfig = cJSON_Parse("{\"v\":1,\"type\":\"service\",\"request_id\":\"x\",\"service\":\"set_config\",\"arguments\":{\"slave_count\":1,\"current_sense_slave\":1,\"shunt_resistance_uohm\":10000,\"battery_capacity_mah\":314000,\"invert_current\":false,\"balance_enabled\":true,\"startup_diagnostics\":true}}" );
        const bool setConfigAccepted = parseService(setConfig, service, arguments, length) && service == Service::SetConfig && length == 13U && arguments[0] == 1U && arguments[1] == 1U && arguments[11] == 1U && arguments[12] == 1U;
        cJSON_Delete(setConfig);
        cJSON *invalidCredentials = cJSON_Parse("{\"v\":1,\"type\":\"wifi_configure\",\"request_id\":\"x\",\"ssid\":\"\",\"password\":\"x\"}");
        const char *ssid = nullptr;
        const char *password = nullptr;
        const bool credentialsRejected = !parseWifiConfigure(invalidCredentials, ssid, password);
        cJSON_Delete(invalidCredentials);
        cJSON *scan = cJSON_Parse("{\"v\":1,\"type\":\"wifi_scan\",\"request_id\":\"x\"}");
        const bool scanAccepted = parseWifiScan(scan);
        cJSON_Delete(scan);
        cJSON *statusMessage = bmsStatusJson();
        cJSON *messageType = cJSON_GetObjectItemCaseSensitive(statusMessage, "type");
        cJSON *messageStatus = cJSON_GetObjectItemCaseSensitive(statusMessage, "status");
        const bool statusMessageValid = cJSON_IsString(messageType) && std::strcmp(messageType->valuestring, "bms_status") == 0 &&
                                        cJSON_IsObject(messageStatus) && cJSON_HasObjectItem(messageStatus, "measurements_fresh");
        cJSON_Delete(statusMessage);
        return serviceRejected && getRtcAccepted && deviceInfoAccepted && getConfigAccepted && getDiagnosticReportAccepted && setConfigAccepted && credentialsRejected && scanAccepted && statusMessageValid;
    }
}

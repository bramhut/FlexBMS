#include "flexbms/GatewayApi.h"
#include "flexbms/GatewayAssets.h"
#include "flexbms/FirmwareUpdate.h"
#include "flexbms/WifiManager.h"

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_timer.h"

#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>

namespace FlexBms::GatewayApi
{
    namespace
    {
        constexpr size_t kMaxBrowserMessageBytes = 4096U;
        constexpr size_t kMaxRequestIdBytes = 64U;
        constexpr size_t kMaxSlaves = 16U;
        httpd_handle_t server = nullptr;
        ServiceSender serviceSender = nullptr;
        bool uartHealthy = false;
        bool serviceInFlight = false;
        bool serviceCapabilityKnown = false;
        bool serviceCapability = false;
        uint8_t pendingSequence = 0U;
        int pendingServiceSocket = -1;
        int64_t pendingServiceStartedUs = 0;
        char pendingRequestId[kMaxRequestIdBytes + 1U] = {};
        Service pendingService = Service::SetRunRequest;
        int pendingScanSocket = -1;
        char pendingScanRequestId[kMaxRequestIdBytes + 1U] = {};
        UartV1::Status status{};
        UartV1::Pack pack{};
        std::array<UartV1::Cell, kMaxSlaves> cells{};
        std::array<UartV1::Temperature, kMaxSlaves> temperatures{};
        std::array<bool, kMaxSlaves> hasCell{};
        std::array<bool, kMaxSlaves> hasTemperature{};
        bool hasStatus = false;
        bool hasPack = false;

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
            case Service::ClearFaults: return "clear_faults";
            case Service::SetRtc: return "set_rtc";
            case Service::GetRtc: return "get_rtc";
            case Service::ReadRegister: return "read_register";
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

        bool sendJsonTo(int socket, cJSON *root)
        {
            char *text = cJSON_PrintUnformatted(root);
            cJSON_Delete(root);
            if (text == nullptr) return false;
            httpd_ws_frame_t frame{};
            frame.type = HTTPD_WS_TYPE_TEXT;
            frame.payload = reinterpret_cast<uint8_t *>(text);
            frame.len = std::strlen(text);
            const esp_err_t result = httpd_ws_send_frame_async(server, socket, &frame);
            cJSON_free(text);
            return result == ESP_OK;
        }

        void broadcast(cJSON *root)
        {
            if (server == nullptr) { cJSON_Delete(root); return; }
            size_t count = 8U;
            std::array<int, 8U> sockets{};
            if (httpd_get_client_list(server, &count, sockets.data()) != ESP_OK) { cJSON_Delete(root); return; }
            char *text = cJSON_PrintUnformatted(root);
            cJSON_Delete(root);
            if (text == nullptr) return;
            for (size_t index = 0U; index < count; ++index)
            {
                httpd_ws_frame_t frame{};
                frame.type = HTTPD_WS_TYPE_TEXT;
                frame.payload = reinterpret_cast<uint8_t *>(text);
                frame.len = std::strlen(text);
                (void)httpd_ws_send_frame_async(server, sockets[index], &frame);
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
            cJSON_AddStringToObject(root, "uart_state", uartHealthy ? "healthy" : (hasStatus ? "lost" : "starting"));
            cJSON_AddStringToObject(root, "mqtt_state", "unavailable");
            cJSON *log = cJSON_AddObjectToObject(root, "diagnostic_log");
            cJSON_AddBoolToObject(log, "available", false);
            cJSON_AddNumberToObject(log, "bytes", 0);
            const FirmwareUpdate::Status &update = FirmwareUpdate::getStatus();
            cJSON *updateJson = cJSON_AddObjectToObject(root, "firmware_update");
            cJSON_AddStringToObject(updateJson, "phase", updatePhaseName(update.phase));
            cJSON_AddStringToObject(updateJson, "target", updateTargetName(update.target));
            cJSON_AddNumberToObject(updateJson, "received_bytes", update.bytesReceived);
            cJSON_AddNumberToObject(updateJson, "expected_bytes", update.bytesExpected);
            cJSON_AddStringToObject(updateJson, "version", update.version);
            cJSON_AddStringToObject(updateJson, "detail", update.detail);
        }

        cJSON *hello()
        {
            const bool bmsServices = Wifi::allowsBmsServices();
            cJSON *root = base("hello");
            cJSON_AddStringToObject(root, "gateway_version", "0.1.0");
            cJSON *caps = cJSON_AddObjectToObject(root, "capabilities");
            cJSON_AddBoolToObject(caps, "monitor", true);
            cJSON_AddBoolToObject(caps, "csv_logging", true);
            cJSON_AddBoolToObject(caps, "set_run_request", bmsServices);
            cJSON_AddBoolToObject(caps, "clear_faults", bmsServices);
            cJSON_AddBoolToObject(caps, "set_rtc", bmsServices);
            cJSON_AddBoolToObject(caps, "get_rtc", bmsServices);
            cJSON_AddBoolToObject(caps, "read_register", bmsServices);
            cJSON_AddBoolToObject(caps, "wifi_configuration", Wifi::getState() != Wifi::State::Unavailable);
            cJSON_AddBoolToObject(caps, "diagnostic_log_download", false);
            cJSON_AddBoolToObject(caps, "raw_terminal", false);
            cJSON_AddBoolToObject(caps, "firmware_update", FirmwareUpdate::isAvailable());
            cJSON *gateway = cJSON_AddObjectToObject(root, "gateway_status");
            addGatewayStatus(gateway);
            return root;
        }

        bool completeSnapshot()
        {
            if (!hasStatus || !hasPack || status.slaveCount > kMaxSlaves || (status.flags & (1U << 3U)) == 0U) return false;
            for (uint8_t index = 0U; index < status.slaveCount; ++index)
            {
                if (!hasCell[index] || !hasTemperature[index]) return false;
            }
            return true;
        }

        void addBmsStatus(cJSON *root)
        {
            cJSON_AddNumberToObject(root, "bms_state", status.bmsState);
            cJSON_AddNumberToObject(root, "hv_state", status.hvState);
            cJSON_AddNumberToObject(root, "flags", status.flags);
            cJSON_AddNumberToObject(root, "slave_count", status.slaveCount);
            cJSON_AddNumberToObject(root, "bms_active_faults", status.bmsActiveFaults);
            cJSON_AddNumberToObject(root, "bms_latched_faults", status.bmsLatchedFaults);
            cJSON_AddNumberToObject(root, "hv_active_faults", status.hvActiveFaults);
            cJSON_AddNumberToObject(root, "hv_latched_faults", status.hvLatchedFaults);
            cJSON_AddNumberToObject(root, "uptime_ms", status.uptimeMs);
            cJSON_AddBoolToObject(root, "measurements_fresh", (status.flags & (1U << 3U)) != 0U);
            cJSON_AddBoolToObject(root, "run_request", (status.flags & (1U << 2U)) != 0U);
        }

        cJSON *bmsStatusJson()
        {
            cJSON *root = base("bms_status");
            cJSON *stateJson = cJSON_AddObjectToObject(root, "status");
            addBmsStatus(stateJson);
            return root;
        }

        cJSON *snapshotJson()
        {
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

        bool parseService(cJSON *root, Service &service, std::array<uint8_t, 4U> &arguments, uint8_t &argumentLength)
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
            if (std::strcmp(name->valuestring, "clear_faults") == 0)
            {
                if (!objectHasExactly(args, {})) return false;
                service = Service::ClearFaults;
                argumentLength = 0U;
                return true;
            }
            uint32_t value = 0U;
            if (std::strcmp(name->valuestring, "set_rtc") == 0)
            {
                if (!objectHasExactly(args, {"unix_time_s"}) || !jsonInteger(cJSON_GetObjectItemCaseSensitive(args, "unix_time_s"), UINT32_MAX, value)) return false;
                service = Service::SetRtc;
                arguments[0] = static_cast<uint8_t>(value);
                arguments[1] = static_cast<uint8_t>(value >> 8U);
                arguments[2] = static_cast<uint8_t>(value >> 16U);
                arguments[3] = static_cast<uint8_t>(value >> 24U);
                argumentLength = 4U;
                return true;
            }
            if (std::strcmp(name->valuestring, "get_rtc") == 0)
            {
                if (!objectHasExactly(args, {})) return false;
                service = Service::GetRtc;
                argumentLength = 0U;
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
            if (!Wifi::allowsBmsServices() || Wifi::isAccessPointActive() || !FirmwareUpdate::isAvailable() || serviceInFlight)
            {
                return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Firmware update is available only on the station LAN");
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
            while (remaining > 0)
            {
                const int received = httpd_req_recv(request, reinterpret_cast<char *>(chunk.data()), std::min<int>(remaining, static_cast<int>(chunk.size())));
                if (received <= 0 || !FirmwareUpdate::writeUpload(chunk.data(), static_cast<size_t>(received)))
                {
                    FirmwareUpdate::abortUpload("Browser upload failed");
                    return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Firmware upload failed");
                }
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
                (void)sendJsonTo(socket, hello());
                if (hasStatus) (void)sendJsonTo(socket, bmsStatusJson());
                if (completeSnapshot()) (void)sendJsonTo(socket, snapshotJson());
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
            if (isService)
            {
                Service requestedService{};
                std::array<uint8_t, 4U> arguments{};
                uint8_t argumentLength = 0U;
                if (parseService(root, requestedService, arguments, argumentLength))
                {
                    const char *requestId = cJSON_GetObjectItemCaseSensitive(root, "request_id")->valuestring;
                    if (!Wifi::allowsBmsServices() || FirmwareUpdate::getStatus().phase == FirmwareUpdate::Phase::Uploading || FirmwareUpdate::getStatus().phase == FirmwareUpdate::Phase::Installing) sendServiceResult(socket, requestId, requestedService, ServiceResult::Denied);
                    else if (serviceInFlight) sendServiceResult(socket, requestId, requestedService, ServiceResult::Busy);
                    else if (!uartHealthy || serviceSender == nullptr) sendServiceResult(socket, requestId, requestedService, ServiceResult::TransportError);
                    else
                    {
                        uint8_t sequence = 0U;
                        if (!serviceSender(requestedService, arguments.data(), argumentLength, sequence)) sendServiceResult(socket, requestId, requestedService, ServiceResult::TransportError);
                        else
                        {
                            std::strncpy(pendingRequestId, requestId, kMaxRequestIdBytes);
                            pendingRequestId[kMaxRequestIdBytes] = '\0';
                            pendingService = requestedService;
                            pendingSequence = sequence;
                            pendingServiceSocket = socket;
                            pendingServiceStartedUs = esp_timer_get_time();
                            serviceInFlight = true;
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
            cJSON_Delete(root);
            return ESP_OK;
        }
    }

    bool start(ServiceSender sender)
    {
        if (server != nullptr) return true;
        serviceSender = sender;
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.lru_purge_enable = true;
        config.max_uri_handlers = 6;
        config.uri_match_fn = httpd_uri_match_wildcard;
        if (httpd_start(&server, &config) != ESP_OK) return false;
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
        Wifi::ScanResults results{};
        if (Wifi::consumeScanResults(results) && pendingScanSocket >= 0)
        {
            sendWifiScanResult(pendingScanSocket, pendingScanRequestId, results.successful ? "ok" : "unavailable", results.successful ? &results : nullptr);
            pendingScanSocket = -1;
            pendingScanRequestId[0] = '\0';
        }
        if (serviceInFlight && esp_timer_get_time() - pendingServiceStartedUs >= 3000000LL)
        {
            sendServiceResult(pendingServiceSocket, pendingRequestId, pendingService, ServiceResult::TransportError);
            serviceInFlight = false;
            pendingServiceSocket = -1;
        }
    }

    void setUartHealthy(bool healthy)
    {
        if (uartHealthy != healthy)
        {
            uartHealthy = healthy;
            publishGatewayStatus();
        }
    }

    void publishGatewayStatus()
    {
        const bool currentServiceCapability = Wifi::allowsBmsServices();
        if (!serviceCapabilityKnown || serviceCapability != currentServiceCapability)
        {
            serviceCapabilityKnown = true;
            serviceCapability = currentServiceCapability;
            broadcast(hello());
        }
        cJSON *root = base("gateway_status");
        addGatewayStatus(root);
        broadcast(root);
    }

    void publishFrame(const UartV1::Frame &frame, uint32_t gatewayUptimeMs)
    {
        if (frame.type == UartV1::MessageType::Status && UartV1::decodeStatus(frame, status))
        {
            hasStatus = true;
            broadcast(bmsStatusJson());
            if ((status.flags & (1U << 3U)) == 0U)
            {
                hasPack = false;
                hasCell.fill(false);
                hasTemperature.fill(false);
            }
        }
        else if (frame.type == UartV1::MessageType::Pack && UartV1::decodePack(frame, pack)) hasPack = true;
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
        else if (frame.type == UartV1::MessageType::Event && frame.length == 3U)
        {
            cJSON *event = base("event");
            cJSON_AddNumberToObject(event, "event_id", frame.payload[0]);
            cJSON_AddNumberToObject(event, "value", static_cast<uint16_t>(frame.payload[1]) | (static_cast<uint16_t>(frame.payload[2]) << 8U));
            cJSON_AddNumberToObject(event, "gateway_uptime_ms", gatewayUptimeMs);
            broadcast(event);
        }
        if (completeSnapshot()) broadcast(snapshotJson());
    }

    void completeService(uint8_t sequence, ServiceResult result, const uint8_t *data, uint8_t dataLength)
    {
        if (!serviceInFlight || sequence != pendingSequence) return;
        cJSON *root = base("service_result");
        cJSON_AddStringToObject(root, "request_id", pendingRequestId);
        cJSON_AddStringToObject(root, "service", serviceName(pendingService));
        cJSON_AddStringToObject(root, "result", resultName(result));
        if (result == ServiceResult::Ok && pendingService == Service::ReadRegister && dataLength == 4U)
        {
            cJSON *value = cJSON_AddObjectToObject(root, "data");
            cJSON_AddNumberToObject(value, "slave_index", data[0]);
            cJSON_AddNumberToObject(value, "register", data[1]);
            cJSON_AddNumberToObject(value, "value", static_cast<uint16_t>(data[2]) | (static_cast<uint16_t>(data[3]) << 8U));
        }
        else if (result == ServiceResult::Ok && pendingService == Service::GetRtc && dataLength == 4U)
        {
            cJSON *value = cJSON_AddObjectToObject(root, "data");
            cJSON_AddNumberToObject(value, "unix_time_s", static_cast<uint32_t>(data[0]) |
                                                            (static_cast<uint32_t>(data[1]) << 8U) |
                                                            (static_cast<uint32_t>(data[2]) << 16U) |
                                                            (static_cast<uint32_t>(data[3]) << 24U));
        }
        (void)sendJsonTo(pendingServiceSocket, root);
        serviceInFlight = false;
        pendingServiceSocket = -1;
    }

    bool serviceBusy() { return serviceInFlight; }

    bool verifyBrowserApi()
    {
        cJSON *bad = cJSON_Parse("{\"v\":1,\"type\":\"service\",\"request_id\":\"x\",\"service\":\"set_run_request\",\"arguments\":{\"requested\":true},\"raw\":\"FB\"}");
        Service service{};
        std::array<uint8_t, 4U> arguments{};
        uint8_t length = 0U;
        const bool serviceRejected = !parseService(bad, service, arguments, length);
        cJSON_Delete(bad);
        cJSON *getRtc = cJSON_Parse("{\"v\":1,\"type\":\"service\",\"request_id\":\"x\",\"service\":\"get_rtc\",\"arguments\":{}}");
        const bool getRtcAccepted = parseService(getRtc, service, arguments, length) && service == Service::GetRtc && length == 0U;
        cJSON_Delete(getRtc);
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
        return serviceRejected && getRtcAccepted && credentialsRejected && scanAccepted && statusMessageValid;
    }
}

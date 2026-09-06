#include "flexbms/MqttClient.h"

#include "cJSON.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "nvs.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace FlexBms::Mqtt
{
    namespace
    {
        constexpr const char *kLogTag = "flexbms_mqtt";
        constexpr const char *kNvsNamespace = "flexbms_mqtt";
        constexpr const char *kHostKey = "host";
        constexpr const char *kPortKey = "port";
        constexpr const char *kUsernameKey = "username";
        constexpr const char *kPasswordKey = "password";
        constexpr size_t kHostBytes = 63U;
        constexpr size_t kUsernameBytes = 63U;
        constexpr size_t kPasswordBytes = 63U;
        constexpr int64_t kStatePeriodUs = 1'000'000LL;
        constexpr int64_t kStartRetryMinimumUs = 5'000'000LL;
        constexpr int64_t kStartRetryMaximumUs = 60'000'000LL;

        struct Credentials
        {
            std::array<char, kHostBytes + 1U> host{};
            std::array<char, kUsernameBytes + 1U> username{};
            std::array<char, kPasswordBytes + 1U> password{};
            uint16_t port = 1883U;
            bool configured = false;
        };

        Credentials credentials{};
        esp_mqtt_client_handle_t client = nullptr;
        RunRequestSender runRequestSender = nullptr;
        State state = State::Unavailable;
        bool stationConnected = false;
        bool uartHealthy = false;
        bool stateDirty = false;
        bool discoveryDirty = false;
        bool statusChanged = false;
        uint8_t startFailureCount = 0U;
        int64_t startNotBeforeUs = 0;
        int64_t lastStatePublishUs = 0;
        UartV1::Status status{};
        UartV1::Pack pack{};
        UartV1::Energy energy{};
        bool hasStatus = false;
        bool hasPack = false;
        bool hasEnergy = false;
        uint32_t lastGatewayUptimeMs = 0U;
        std::array<char, 24U> deviceId{};
        std::array<char, 96U> baseTopic{};
        std::array<char, 128U> availabilityTopic{};
        std::array<char, 128U> stateTopic{};
        std::array<char, 128U> eventTopic{};
        std::array<char, 128U> commandTopic{};
        std::array<char, 128U> discoveryTopic{};
        std::array<char, 80U> brokerUri{};

        bool validText(const char *value, size_t maximum, bool allowEmpty)
        {
            if (value == nullptr) return false;
            const size_t length = strnlen(value, maximum + 1U);
            return (allowEmpty || length > 0U) && length <= maximum;
        }

        bool validHost(const char *value)
        {
            if (!validText(value, kHostBytes, false)) return false;
            for (const char *cursor = value; *cursor != '\0'; ++cursor)
            {
                const bool allowed = (*cursor >= 'a' && *cursor <= 'z') || (*cursor >= 'A' && *cursor <= 'Z') ||
                                     (*cursor >= '0' && *cursor <= '9') || *cursor == '.' || *cursor == '-' || *cursor == ':' ||
                                     *cursor == '[' || *cursor == ']';
                if (!allowed) return false;
            }
            return true;
        }

        bool readString(nvs_handle_t handle, const char *key, char *target, size_t capacity)
        {
            size_t bytes = capacity;
            return nvs_get_str(handle, key, target, &bytes) == ESP_OK && target[0] != '\0';
        }

        void loadCredentials()
        {
            nvs_handle_t handle{};
            if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) return;
            uint16_t port = 0U;
            credentials.configured = readString(handle, kHostKey, credentials.host.data(), credentials.host.size()) &&
                                     nvs_get_u16(handle, kPortKey, &port) == ESP_OK && port != 0U &&
                                     readString(handle, kUsernameKey, credentials.username.data(), credentials.username.size()) &&
                                     readString(handle, kPasswordKey, credentials.password.data(), credentials.password.size());
            credentials.port = credentials.configured ? port : 1883U;
            nvs_close(handle);
        }

        bool saveCredentials()
        {
            nvs_handle_t handle{};
            esp_err_t result = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
            if (result == ESP_OK) result = nvs_set_str(handle, kHostKey, credentials.host.data());
            if (result == ESP_OK) result = nvs_set_u16(handle, kPortKey, credentials.port);
            if (result == ESP_OK) result = nvs_set_str(handle, kUsernameKey, credentials.username.data());
            if (result == ESP_OK) result = nvs_set_str(handle, kPasswordKey, credentials.password.data());
            if (result == ESP_OK) result = nvs_commit(handle);
            if (handle != 0U) nvs_close(handle);
            return result == ESP_OK;
        }

        void buildTopics()
        {
            uint8_t mac[6]{};
            (void)esp_read_mac(mac, ESP_MAC_WIFI_STA);
            std::snprintf(deviceId.data(), deviceId.size(), "flexbms_%02X%02X%02X", mac[3], mac[4], mac[5]);
            std::snprintf(baseTopic.data(), baseTopic.size(), "flexbms/%s", deviceId.data());
            std::snprintf(availabilityTopic.data(), availabilityTopic.size(), "%s/availability", baseTopic.data());
            std::snprintf(stateTopic.data(), stateTopic.size(), "%s/state", baseTopic.data());
            std::snprintf(eventTopic.data(), eventTopic.size(), "%s/event", baseTopic.data());
            std::snprintf(commandTopic.data(), commandTopic.size(), "%s/command/run_request", baseTopic.data());
            std::snprintf(discoveryTopic.data(), discoveryTopic.size(), "homeassistant/device/%s/config", deviceId.data());
        }

        void publish(const char *topic, const char *payload, bool retained)
        {
            if (client == nullptr || state != State::Connected) return;
            (void)esp_mqtt_client_publish(client, topic, payload, 0, 1, retained ? 1 : 0);
        }

        void addComponent(cJSON *components, const char *key, const char *platform, const char *name, const char *uniqueId, const char *templateText,
                          const char *deviceClass = nullptr, const char *unit = nullptr, const char *entityCategory = nullptr,
                          int suggestedDisplayPrecision = -1, const char *stateClass = nullptr)
        {
            cJSON *component = cJSON_AddObjectToObject(components, key);
            cJSON_AddStringToObject(component, "p", platform);
            cJSON_AddStringToObject(component, "name", name);
            cJSON_AddStringToObject(component, "unique_id", uniqueId);
            if (templateText != nullptr) cJSON_AddStringToObject(component, "value_template", templateText);
            if (deviceClass != nullptr) cJSON_AddStringToObject(component, "device_class", deviceClass);
            if (unit != nullptr) cJSON_AddStringToObject(component, "unit_of_measurement", unit);
            if (entityCategory != nullptr) cJSON_AddStringToObject(component, "entity_category", entityCategory);
            if (suggestedDisplayPrecision >= 0) cJSON_AddNumberToObject(component, "suggested_display_precision", suggestedDisplayPrecision);
            if (stateClass != nullptr) cJSON_AddStringToObject(component, "state_class", stateClass);
        }

        void publishDiscovery()
        {
            if (state != State::Connected) return;
            cJSON *root = cJSON_CreateObject();
            cJSON *device = cJSON_AddObjectToObject(root, "dev");
            cJSON *identifiers = cJSON_AddArrayToObject(device, "ids");
            cJSON_AddItemToArray(identifiers, cJSON_CreateString(deviceId.data()));
            cJSON_AddStringToObject(device, "name", "FlexBMS Home BESS");
            cJSON_AddStringToObject(device, "mf", "FlexBMS");
            cJSON_AddStringToObject(device, "mdl", "ESP32 Gateway");
            cJSON_AddStringToObject(device, "configuration_url", "http://flexbms.local/");
            cJSON *origin = cJSON_AddObjectToObject(root, "o");
            cJSON_AddStringToObject(origin, "name", "FlexBMS Gateway");
            cJSON_AddStringToObject(origin, "sw", "1");
            cJSON_AddStringToObject(root, "avty_t", availabilityTopic.data());
            cJSON_AddStringToObject(root, "stat_t", stateTopic.data());
            cJSON_AddNumberToObject(root, "qos", 1);
            cJSON *components = cJSON_AddObjectToObject(root, "cmps");
            char uniqueId[48]{};
            auto component = [&](const char *key, const char *platform, const char *name, const char *tpl, const char *deviceClass = nullptr,
                                 const char *unit = nullptr, const char *category = nullptr, int suggestedDisplayPrecision = -1,
                                 const char *stateClass = nullptr)
            {
                std::snprintf(uniqueId, sizeof(uniqueId), "%s_%s", deviceId.data(), key);
                addComponent(components, key, platform, name, uniqueId, tpl, deviceClass, unit, category, suggestedDisplayPrecision, stateClass);
            };
            component("run_request", "switch", "Run request", "{{ 'ON' if value_json.run_request else 'OFF' }}");
            cJSON *runRequest = cJSON_GetObjectItemCaseSensitive(components, "run_request");
            cJSON_AddStringToObject(runRequest, "cmd_t", commandTopic.data());
            cJSON_AddStringToObject(runRequest, "pl_on", "ON");
            cJSON_AddStringToObject(runRequest, "pl_off", "OFF");
            cJSON_AddBoolToObject(runRequest, "opt", false);
            component("pack_voltage", "sensor", "Pack voltage", "{{ value_json.pack_voltage_v }}", "voltage", "V", nullptr, -1, "measurement");
            component("pack_current", "sensor", "Pack current", "{{ value_json.pack_current_a if value_json.current_valid else none }}", "current", "A", nullptr, -1, "measurement");
            component("pack_power", "sensor", "Pack power", "{{ value_json.pack_power_w if value_json.current_valid else none }}", "power", "W", nullptr, -1, "measurement");
            component("state_of_charge", "sensor", "State of charge", "{{ value_json.soc_percent if value_json.soc_valid else none }}", "battery", "%", nullptr, -1, "measurement");
            component("min_cell_voltage", "sensor", "Minimum cell voltage", "{{ value_json.min_cell_v }}", "voltage", "V", nullptr, 3, "measurement");
            component("max_cell_voltage", "sensor", "Maximum cell voltage", "{{ value_json.max_cell_v }}", "voltage", "V", nullptr, 3, "measurement");
            component("cell_delta", "sensor", "Cell voltage delta", "{{ value_json.cell_delta_mv }}", nullptr, "mV", nullptr, 0, "measurement");
            component("min_ntc_temperature", "sensor", "Minimum NTC temperature", "{{ value_json.min_ntc_c }}", "temperature", "°C", nullptr, -1, "measurement");
            component("max_ntc_temperature", "sensor", "Maximum NTC temperature", "{{ value_json.max_ntc_c }}", "temperature", "°C", nullptr, -1, "measurement");
            component("battery_energy_charged", "sensor", "Battery energy charged", "{{ value_json.charged_energy_kwh if value_json.energy_valid else none }}", "energy", "kWh", nullptr, 3, "total_increasing");
            component("battery_energy_discharged", "sensor", "Battery energy discharged", "{{ value_json.discharged_energy_kwh if value_json.energy_valid else none }}", "energy", "kWh", nullptr, 3, "total_increasing");
            component("bms_state", "sensor", "BMS state", "{{ value_json.bms_state }}", nullptr, nullptr, "diagnostic");
            component("hv_state", "sensor", "HV state", "{{ value_json.hv_state }}", nullptr, nullptr, "diagnostic");
            component("telemetry_fresh", "binary_sensor", "Telemetry fresh", "{{ 'ON' if value_json.measurements_fresh else 'OFF' }}", nullptr, nullptr, "diagnostic");
            component("hv_running", "binary_sensor", "HV running", "{{ 'ON' if value_json.hv_state == 'RUN' else 'OFF' }}", nullptr, nullptr, "diagnostic");
            component("fault_active", "binary_sensor", "Fault active", "{{ 'ON' if value_json.fault_active else 'OFF' }}", "problem", nullptr, "diagnostic");
            component("active_faults", "sensor", "Active faults", "{{ value_json.active_faults }}", nullptr, nullptr, "diagnostic");
            component("uart_healthy", "binary_sensor", "UART healthy", "{{ 'ON' if value_json.uart_healthy else 'OFF' }}", "connectivity", nullptr, "diagnostic");
            component("mqtt_state", "sensor", "MQTT state", "{{ value_json.mqtt_state }}", nullptr, nullptr, "diagnostic");
            component("gateway_uptime", "sensor", "Gateway uptime", "{{ value_json.gateway_uptime_s }}", "duration", "s", "diagnostic");
            component("stm32_uptime", "sensor", "BMS controller uptime", "{{ value_json.stm32_uptime_s }}", "duration", "s", "diagnostic");
            char *text = cJSON_PrintUnformatted(root);
            if (text != nullptr) { publish(discoveryTopic.data(), text, true); cJSON_free(text); }
            cJSON_Delete(root);
        }

        const char *bmsStateName(uint8_t value) { static constexpr const char *names[] = {"STARTING", "READY", "RUNNING", "ERROR", "CRITICAL"}; return value < 5U ? names[value] : "UNKNOWN"; }
        const char *hvStateName(uint8_t value) { static constexpr const char *names[] = {"OFF", "SELF_TEST", "PRECHARGE", "CONTACTOR_CLOSE", "RUN"}; return value < 5U ? names[value] : "UNKNOWN"; }
        void appendMask(cJSON *array, uint32_t mask, const char *const *names, size_t count)
        {
            for (size_t bit = 0U; bit < count; ++bit) if ((mask & (1UL << bit)) != 0U) cJSON_AddItemToArray(array, cJSON_CreateString(names[bit]));
        }

        void publishState()
        {
            if (state != State::Connected || !hasStatus) return;
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "bms_state", bmsStateName(status.bmsState));
            cJSON_AddStringToObject(root, "hv_state", hvStateName(status.hvState));
            cJSON_AddBoolToObject(root, "run_request", (status.flags & (1U << 2U)) != 0U);
            const bool fresh = (status.flags & (1U << 3U)) != 0U;
            cJSON_AddBoolToObject(root, "measurements_fresh", fresh);
            cJSON_AddBoolToObject(root, "current_valid", (status.flags & (1U << 7U)) != 0U);
            cJSON_AddBoolToObject(root, "soc_valid", (status.flags & (1U << 6U)) != 0U);
            cJSON_AddBoolToObject(root, "energy_valid", hasEnergy && energy.valid);
            if (hasEnergy && energy.valid)
            {
                cJSON_AddNumberToObject(root, "charged_energy_kwh", static_cast<double>(energy.chargedEnergyUWh) / 1'000'000'000.0);
                cJSON_AddNumberToObject(root, "discharged_energy_kwh", static_cast<double>(energy.dischargedEnergyUWh) / 1'000'000'000.0);
            }
            cJSON_AddBoolToObject(root, "uart_healthy", uartHealthy);
            cJSON_AddStringToObject(root, "mqtt_state", "connected");
            cJSON_AddNumberToObject(root, "gateway_uptime_s", lastGatewayUptimeMs / 1000U);
            cJSON_AddNumberToObject(root, "stm32_uptime_s", status.uptimeMs / 1000U);
            static constexpr const char *bmsFaults[] = {"CONFIGURATION_INVALID", "SLAVE_UNAVAILABLE", "BCC_DIAGNOSTICS", "CELL_VOLTAGE_LIMIT", "THERMAL_LIMIT", "CURRENT_LIMIT", "BCC_INTEGRITY", "ADC_FAULT", "BALANCING_HARDWARE_FAULT", "BCC_COMMUNICATION"};
            static constexpr const char *hvFaults[] = {"HV_SENSOR_DIAGNOSTIC", "BATTERY_VOLTAGE_MISMATCH", "LOAD_SIDE_ENERGISED", "PRECHARGE_TIMEOUT", "PRECHARGE_VOLTAGE_LOST", "CONTACTOR_VOLTAGE_LOST"};
            const bool activeFault = status.bmsActiveErrors != 0U || status.hvActiveErrors != 0U || status.bmsState >= 3U;
            cJSON_AddBoolToObject(root, "fault_active", activeFault);
            cJSON *faults = cJSON_AddArrayToObject(root, "fault_names");
            appendMask(faults, status.bmsActiveErrors, bmsFaults, std::size(bmsFaults));
            appendMask(faults, status.hvActiveErrors, hvFaults, std::size(hvFaults));
            char faultText[256]{};
            size_t faultLength = 0U;
            auto appendNames = [&](uint32_t mask, const char *const *names, size_t count)
            {
                for (size_t bit = 0U; bit < count; ++bit)
                {
                    if ((mask & (1UL << bit)) == 0U) continue;
                    const int written = std::snprintf(faultText + faultLength, sizeof(faultText) - faultLength, "%s%s", faultLength == 0U ? "" : ", ", names[bit]);
                    if (written <= 0 || static_cast<size_t>(written) >= sizeof(faultText) - faultLength) { faultLength = sizeof(faultText) - 1U; return; }
                    faultLength += static_cast<size_t>(written);
                }
            };
            appendNames(status.bmsActiveErrors, bmsFaults, std::size(bmsFaults));
            appendNames(status.hvActiveErrors, hvFaults, std::size(hvFaults));
            cJSON_AddStringToObject(root, "active_faults", faultLength == 0U ? "No faults" : faultText);
            if (hasPack && fresh)
            {
                const float voltage = static_cast<float>(pack.packVoltageUv) / 1'000'000.0F;
                const float current = static_cast<float>(pack.packCurrentRaw) / 64.0F;
                cJSON_AddNumberToObject(root, "pack_voltage_v", voltage);
                cJSON_AddNumberToObject(root, "pack_current_a", current);
                cJSON_AddNumberToObject(root, "pack_power_w", voltage * current);
                cJSON_AddNumberToObject(root, "soc_percent", 100.0F * (static_cast<float>(pack.socRaw) / 65535.0F * 3.0F - 1.0F));
                cJSON_AddNumberToObject(root, "min_cell_v", static_cast<float>(pack.minCellUv) / 1'000'000.0F);
                cJSON_AddNumberToObject(root, "max_cell_v", static_cast<float>(pack.maxCellUv) / 1'000'000.0F);
                cJSON_AddNumberToObject(root, "cell_delta_mv", static_cast<float>(pack.maxCellUv - pack.minCellUv) / 1'000.0F);
                cJSON_AddNumberToObject(root, "min_ntc_c", static_cast<float>(pack.minNtcRaw) / 65535.0F * 120.0F - 20.0F);
                cJSON_AddNumberToObject(root, "max_ntc_c", static_cast<float>(pack.maxNtcRaw) / 65535.0F * 120.0F - 20.0F);
            }
            char *text = cJSON_PrintUnformatted(root);
            if (text != nullptr) { publish(stateTopic.data(), text, true); cJSON_free(text); }
            cJSON_Delete(root);
        }

        void publishEvent(uint8_t id, uint32_t value)
        {
            cJSON *root = cJSON_CreateObject();
            cJSON_AddNumberToObject(root, "event_id", id);
            cJSON_AddNumberToObject(root, "value", value);
            cJSON_AddNumberToObject(root, "gateway_uptime_ms", lastGatewayUptimeMs);
            char *text = cJSON_PrintUnformatted(root);
            if (text != nullptr) { publish(eventTopic.data(), text, false); cJSON_free(text); }
            cJSON_Delete(root);
        }

        void deferClientStart(const char *reason)
        {
            const uint8_t shift = std::min<uint8_t>(startFailureCount, 3U);
            const int64_t delay = std::min(kStartRetryMinimumUs << shift, kStartRetryMaximumUs);
            if (startFailureCount < 0xFFU) ++startFailureCount;
            startNotBeforeUs = esp_timer_get_time() + delay;
            ESP_LOGW(kLogTag, "%s; retrying MQTT start in %lld s (free=%u B, largest=%u B, minimum=%u B)", reason,
                     static_cast<long long>(delay / 1'000'000LL),
                     static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
                     static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
                     static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)));
        }

        void startClient()
        {
            if (client != nullptr || !credentials.configured || !stationConnected || esp_timer_get_time() < startNotBeforeUs) return;
            std::snprintf(brokerUri.data(), brokerUri.size(), "mqtt://%s:%u", credentials.host.data(), credentials.port);
            esp_mqtt_client_config_t config{};
            config.broker.address.uri = brokerUri.data();
            config.credentials.username = credentials.username.data();
            config.credentials.authentication.password = credentials.password.data();
            config.credentials.client_id = deviceId.data();
            config.session.last_will.topic = availabilityTopic.data();
            config.session.last_will.msg = "offline";
            config.session.last_will.msg_len = 7U;
            config.session.last_will.qos = 1;
            config.session.last_will.retain = true;
            client = esp_mqtt_client_init(&config);
            if (client == nullptr)
            {
                state = State::Lost;
                statusChanged = true;
                deferClientStart("MQTT client allocation failed");
                return;
            }
            (void)esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, [](void *, esp_event_base_t, int32_t eventId, void *eventData)
            {
                auto *event = static_cast<esp_mqtt_event_handle_t>(eventData);
                if (eventId == MQTT_EVENT_CONNECTED)
                {
                    state = State::Connected;
                    startFailureCount = 0U;
                    startNotBeforeUs = 0;
                    statusChanged = true;
                    discoveryDirty = true;
                    stateDirty = true;
                    publish(availabilityTopic.data(), "online", true);
                    (void)esp_mqtt_client_subscribe(event->client, commandTopic.data(), 1);
                    (void)esp_mqtt_client_subscribe(event->client, "homeassistant/status", 0);
                }
                else if (eventId == MQTT_EVENT_DISCONNECTED) { state = State::Lost; statusChanged = true; }
                else if (eventId == MQTT_EVENT_DATA && event->topic_len == 20 && std::memcmp(event->topic, "homeassistant/status", 20) == 0 &&
                         event->data_len == 6 && std::memcmp(event->data, "online", 6) == 0)
                {
                    discoveryDirty = true;
                    stateDirty = true;
                }
                else if (eventId == MQTT_EVENT_DATA && event->topic_len == static_cast<int>(std::strlen(commandTopic.data())) &&
                         std::memcmp(event->topic, commandTopic.data(), event->topic_len) == 0 && runRequestSender != nullptr)
                {
                    if (event->data_len == 2 && std::memcmp(event->data, "ON", 2) == 0) (void)runRequestSender(true);
                    else if (event->data_len == 3 && std::memcmp(event->data, "OFF", 3) == 0) (void)runRequestSender(false);
                    else ESP_LOGW(kLogTag, "Rejected invalid run-request MQTT payload");
                }
            }, nullptr);
            state = State::Connecting;
            statusChanged = true;
            if (esp_mqtt_client_start(client) != ESP_OK)
            {
                esp_mqtt_client_destroy(client);
                client = nullptr;
                state = State::Lost;
                statusChanged = true;
                deferClientStart("MQTT task start failed");
            }
        }

        void stopClient()
        {
            if (client == nullptr) return;
            (void)esp_mqtt_client_stop(client);
            esp_mqtt_client_destroy(client);
            client = nullptr;
            state = credentials.configured ? State::Lost : State::Unavailable;
            startFailureCount = 0U;
            startNotBeforeUs = 0;
            statusChanged = true;
        }
    }

    bool start(RunRequestSender sender)
    {
        runRequestSender = sender;
        buildTopics();
        loadCredentials();
        state = credentials.configured ? State::Lost : State::Unavailable;
        statusChanged = true;
        return true;
    }

    void tick(bool connected)
    {
        if (stationConnected != connected)
        {
            stationConnected = connected;
            if (!stationConnected) stopClient();
        }
        if (stationConnected) startClient();
        const int64_t now = esp_timer_get_time();
        if (state == State::Connected && discoveryDirty) { publishDiscovery(); discoveryDirty = false; }
        if (state == State::Connected && stateDirty && now - lastStatePublishUs >= kStatePeriodUs)
        {
            publishState();
            lastStatePublishUs = now;
            stateDirty = false;
        }
    }

    State getState() { return state; }
    const char *stateName(State value) { switch (value) { case State::Connecting: return "connecting"; case State::Connected: return "connected"; case State::Lost: return "lost"; default: return "unavailable"; } }
    bool consumeStatusChanged() { const bool changed = statusChanged; statusChanged = false; return changed; }
    Settings getSettings() { return {.configured = credentials.configured, .host = credentials.host.data(), .port = credentials.port, .username = credentials.username.data()}; }

    bool configure(const char *host, uint16_t port, const char *username, const char *password)
    {
        if (!validHost(host) || !validText(username, kUsernameBytes, false) || !validText(password, kPasswordBytes, false) || port == 0U) return false;
        std::strncpy(credentials.host.data(), host, credentials.host.size() - 1U);
        std::strncpy(credentials.username.data(), username, credentials.username.size() - 1U);
        std::strncpy(credentials.password.data(), password, credentials.password.size() - 1U);
        credentials.port = port;
        credentials.configured = true;
        if (!saveCredentials()) return false;
        stopClient();
        return true;
    }

    void publishFrame(const UartV1::Frame &frame, uint32_t gatewayUptimeMs)
    {
        lastGatewayUptimeMs = gatewayUptimeMs;
        if (frame.type == UartV1::MessageType::Status && UartV1::decodeStatus(frame, status))
        {
            hasStatus = true;
            if ((status.flags & (1U << 3U)) == 0U) hasEnergy = false;
            stateDirty = true;
        }
        else if (frame.type == UartV1::MessageType::Pack && UartV1::decodePack(frame, pack)) { hasPack = true; stateDirty = true; }
        else if (frame.type == UartV1::MessageType::Energy && UartV1::decodeEnergy(frame, energy)) { hasEnergy = true; stateDirty = true; }
        else if (frame.type == UartV1::MessageType::Event && frame.length == 5U)
        {
            const uint32_t value = static_cast<uint32_t>(frame.payload[1]) | (static_cast<uint32_t>(frame.payload[2]) << 8U) |
                                   (static_cast<uint32_t>(frame.payload[3]) << 16U) | (static_cast<uint32_t>(frame.payload[4]) << 24U);
            publishEvent(frame.payload[0], value);
            stateDirty = true;
        }
    }

    void setUartHealthy(bool healthy) { if (uartHealthy != healthy) { uartHealthy = healthy; stateDirty = true; } }
}

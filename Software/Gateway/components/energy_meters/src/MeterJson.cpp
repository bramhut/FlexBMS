#include "flexbms/MeterJson.h"
#include <initializer_list>

namespace FlexBms::EnergyMeters
{
    namespace
    {
        bool exactly(cJSON *object, std::initializer_list<const char *> names)
        {
            if (!cJSON_IsObject(object) || cJSON_GetArraySize(object) != static_cast<int>(names.size())) return false;
            for (const char *name : names) if (cJSON_GetObjectItemCaseSensitive(object, name) == nullptr) return false;
            return true;
        }
        bool integer(cJSON *item, uint32_t maximum, uint32_t &value)
        {
            if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) || item->valuedouble < 0 || item->valuedouble > maximum || std::floor(item->valuedouble) != item->valuedouble) return false;
            value = static_cast<uint32_t>(item->valuedouble); return true;
        }
        const char *parityName(Parity parity) { return parity == Parity::Even ? "even" : parity == Parity::Odd ? "odd" : "none"; }
        void age(cJSON *object, const char *key, bool valid, int64_t sampledUs, int64_t nowUs)
        {
            if (valid && nowUs >= sampledUs) cJSON_AddNumberToObject(object, key, static_cast<double>(nowUs - sampledUs) / 1000.0);
            else cJSON_AddNullToObject(object, key);
        }
    }
    bool parseConfiguration(cJSON *json, Configuration &configuration)
    {
        if (!exactly(json, {"baud_rate", "parity", "stop_bits", "meters"})) return false;
        Configuration candidate = defaults();
        uint32_t baud = 0, stop = 0;
        cJSON *parity = cJSON_GetObjectItemCaseSensitive(json, "parity");
        cJSON *meters = cJSON_GetObjectItemCaseSensitive(json, "meters");
        if (!integer(cJSON_GetObjectItemCaseSensitive(json, "baud_rate"), 19200, baud) ||
            !integer(cJSON_GetObjectItemCaseSensitive(json, "stop_bits"), 2, stop) || !cJSON_IsString(parity) ||
            !cJSON_IsArray(meters) || cJSON_GetArraySize(meters) != kMeterCount) return false;
        if (std::strcmp(parity->valuestring, "none") == 0) candidate.parity = Parity::None;
        else if (std::strcmp(parity->valuestring, "even") == 0) candidate.parity = Parity::Even;
        else if (std::strcmp(parity->valuestring, "odd") == 0) candidate.parity = Parity::Odd;
        else return false;
        candidate.baudRate = baud; candidate.stopBits = static_cast<uint8_t>(stop);
        for (size_t slot = 0; slot < kMeterCount; ++slot)
        {
            cJSON *meter = cJSON_GetArrayItem(meters, static_cast<int>(slot));
            if (!exactly(meter, {"enabled", "name", "address", "reverse_power_direction"})) return false;
            cJSON *enabled = cJSON_GetObjectItemCaseSensitive(meter, "enabled");
            cJSON *reverse = cJSON_GetObjectItemCaseSensitive(meter, "reverse_power_direction");
            cJSON *name = cJSON_GetObjectItemCaseSensitive(meter, "name");
            uint32_t address = 0;
            if (!cJSON_IsBool(enabled) || !cJSON_IsBool(reverse) || !cJSON_IsString(name) || std::strlen(name->valuestring) > kNameBytes ||
                !integer(cJSON_GetObjectItemCaseSensitive(meter, "address"), 247, address)) return false;
            auto &target = candidate.meters[slot];
            target.enabled = cJSON_IsTrue(enabled); target.reversePowerDirection = cJSON_IsTrue(reverse);
            target.address = static_cast<uint8_t>(address);
            std::memcpy(target.name.data(), name->valuestring, std::strlen(name->valuestring) + 1);
        }
        if (!valid(candidate)) return false;
        configuration = candidate; return true;
    }
    cJSON *configurationJson(const Configuration &configuration)
    {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "baud_rate", configuration.baudRate);
        cJSON_AddStringToObject(root, "parity", parityName(configuration.parity));
        cJSON_AddNumberToObject(root, "stop_bits", configuration.stopBits);
        cJSON *meters = cJSON_AddArrayToObject(root, "meters");
        for (const auto &meter : configuration.meters)
        {
            cJSON *item = cJSON_CreateObject(); cJSON_AddItemToArray(meters, item);
            cJSON_AddBoolToObject(item, "enabled", meter.enabled);
            cJSON_AddStringToObject(item, "name", meter.name.data());
            cJSON_AddNumberToObject(item, "address", meter.address);
            cJSON_AddBoolToObject(item, "reverse_power_direction", meter.reversePowerDirection);
        }
        return root;
    }
    cJSON *statusJson(const State &state, int64_t nowUs)
    {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "configuration", configurationJson(state.configuration));
        cJSON_AddBoolToObject(root, "stored_configuration_valid", state.storedConfigurationValid);
        cJSON_AddStringToObject(root, "bus_state", !state.available ? "unavailable" : !anyEnabled(state.configuration) ? "disabled" : state.busReady ? "ready" : state.busError ? "error" : "starting");
        cJSON_AddNumberToObject(root, "bus_error", state.busError);
        cJSON *meters = cJSON_AddArrayToObject(root, "meters");
        for (size_t slot = 0; slot < kMeterCount; ++slot)
        {
            const auto &reading = state.readings[slot];
            const bool electricalFresh = state.configuration.meters[slot].enabled && fresh(reading.electricalValid, reading.electricalUs, nowUs, kElectricalFreshUs);
            const bool energyFresh = state.configuration.meters[slot].enabled && fresh(reading.energyValid, reading.energyUs, nowUs, kEnergyFreshUs);
            cJSON *item = cJSON_CreateObject(); cJSON_AddItemToArray(meters, item);
            cJSON_AddBoolToObject(item, "available", state.busReady && electricalFresh);
            cJSON_AddBoolToObject(item, "energy_fresh", energyFresh);
            cJSON_AddNumberToObject(item, "failed_reads", reading.failedReads);
            cJSON_AddNumberToObject(item, "last_error", reading.lastError);
            // Preserve age of the last success even after a failed transaction.
            age(item, "last_success_age_ms", reading.electricalUs != 0, reading.electricalUs, nowUs);
            if (electricalFresh) cJSON_AddItemToObject(item, "electrical", electricalJson(reading, state.configuration.meters[slot].reversePowerDirection, nowUs));
            if (energyFresh) cJSON_AddItemToObject(item, "energy", energyJson(reading, state.configuration.meters[slot].reversePowerDirection));
            if (reading.identityValid)
            {
                cJSON_AddNumberToObject(item, "serial_number", reading.serialNumber);
                cJSON_AddNumberToObject(item, "meter_code", reading.meterCode);
            }
            if (reading.firmwareValid)
            {
                char version[16]{};
                std::snprintf(version, sizeof(version), "%u.%u", reading.firmwareVersion >> 8U, reading.firmwareVersion & 255U);
                cJSON_AddStringToObject(item, "firmware_version", version);
            }
        }
        return root;
    }
    cJSON *electricalJson(const Reading &source, bool reverse, int64_t nowUs)
    {
        const Reading reading = reported(source, reverse);
        cJSON *root = cJSON_CreateObject();
        char key[32]{};
        for (size_t phase = 0; phase < 3; ++phase)
        {
            std::snprintf(key, sizeof(key), "current_l%u_a", static_cast<unsigned>(phase + 1));
            cJSON_AddNumberToObject(root, key, reading.currentA[phase]);
            std::snprintf(key, sizeof(key), "power_l%u_w", static_cast<unsigned>(phase + 1));
            cJSON_AddNumberToObject(root, key, reading.powerW[phase]);
        }
        cJSON_AddNumberToObject(root, "total_power_w", reading.totalPowerW);
        age(root, "sample_age_ms", true, reading.electricalUs, nowUs);
        cJSON_AddNumberToObject(root, "failed_reads", reading.failedReads);
        return root;
    }
    cJSON *energyJson(const Reading &source, bool reverse)
    {
        const Reading reading = reported(source, reverse);
        cJSON *root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "import_kwh", reading.importKwh);
        cJSON_AddNumberToObject(root, "export_kwh", reading.exportKwh);
        return root;
    }
    cJSON *discoveryJson(const State &state, size_t slot, const char *gatewayId, const char *baseTopic)
    {
        cJSON *root = cJSON_CreateObject();
        char id[48]{}, topic[160]{};
        std::snprintf(id, sizeof(id), "%s_meter%u", gatewayId, static_cast<unsigned>(slot + 1));
        cJSON *device = cJSON_AddObjectToObject(root, "dev");
        cJSON *ids = cJSON_AddArrayToObject(device, "ids"); cJSON_AddItemToArray(ids, cJSON_CreateString(id));
        cJSON_AddStringToObject(device, "name", state.configuration.meters[slot].name.data());
        cJSON_AddStringToObject(device, "mf", "Eastron");
        cJSON_AddStringToObject(device, "mdl", "SDM72D-M-2");
        cJSON_AddStringToObject(device, "via_device", gatewayId);
        cJSON_AddStringToObject(device, "configuration_url", "http://flexbms.local/");
        cJSON *origin = cJSON_AddObjectToObject(root, "o"); cJSON_AddStringToObject(origin, "name", "FlexBMS Gateway");
        cJSON_AddNumberToObject(root, "qos", 1);
        cJSON_AddStringToObject(root, "avty_mode", "all");
        cJSON *availability = cJSON_AddArrayToObject(root, "avty");
        cJSON *gateway = cJSON_CreateObject(); cJSON_AddItemToArray(availability, gateway);
        std::snprintf(topic, sizeof(topic), "%s/availability", baseTopic); cJSON_AddStringToObject(gateway, "topic", topic);
        cJSON *meter = cJSON_CreateObject(); cJSON_AddItemToArray(availability, meter);
        std::snprintf(topic, sizeof(topic), "%s/meter%u/availability", baseTopic, static_cast<unsigned>(slot + 1)); cJSON_AddStringToObject(meter, "topic", topic);
        cJSON *components = cJSON_AddObjectToObject(root, "cmps");
        auto sensor = [&](const char *key, const char *name, const char *deviceClass, const char *unit, bool energy, bool diagnostic = false)
        {
            cJSON *item = cJSON_AddObjectToObject(components, key);
            cJSON_AddStringToObject(item, "p", "sensor"); cJSON_AddStringToObject(item, "name", name);
            char unique[96]{}, valueTemplate[96]{};
            std::snprintf(unique, sizeof(unique), "%s_%s", id, key); cJSON_AddStringToObject(item, "unique_id", unique);
            std::snprintf(valueTemplate, sizeof(valueTemplate), "{{ value_json.%s }}", key); cJSON_AddStringToObject(item, "value_template", valueTemplate);
            std::snprintf(topic, sizeof(topic), "%s/meter%u/%s", baseTopic, static_cast<unsigned>(slot + 1), energy ? "energy" : "state");
            cJSON_AddStringToObject(item, "stat_t", topic);
            if (deviceClass != nullptr) cJSON_AddStringToObject(item, "device_class", deviceClass);
            if (unit != nullptr) cJSON_AddStringToObject(item, "unit_of_measurement", unit);
            cJSON_AddStringToObject(item, "state_class", energy ? "total_increasing" : "measurement");
            cJSON_AddNumberToObject(item, "expire_after", energy ? 90 : 3);
            if (energy)
            {
                cJSON_AddStringToObject(item, "avty_mode", "all");
                cJSON *checks = cJSON_Duplicate(availability, true);
                cJSON_AddItemToObject(item, "avty", checks);
                cJSON *check = cJSON_CreateObject(); cJSON_AddItemToArray(checks, check);
                std::snprintf(topic, sizeof(topic), "%s/meter%u/energy_availability", baseTopic, static_cast<unsigned>(slot + 1));
                cJSON_AddStringToObject(check, "topic", topic);
            }
            if (diagnostic) cJSON_AddStringToObject(item, "entity_category", "diagnostic");
        };
        char key[32]{}, name[32]{};
        for (size_t phase = 0; phase < 3; ++phase)
        {
            std::snprintf(key, sizeof(key), "current_l%u_a", static_cast<unsigned>(phase + 1)); std::snprintf(name, sizeof(name), "L%u current", static_cast<unsigned>(phase + 1));
            sensor(key, name, "current", "A", false);
            std::snprintf(key, sizeof(key), "power_l%u_w", static_cast<unsigned>(phase + 1)); std::snprintf(name, sizeof(name), "L%u power", static_cast<unsigned>(phase + 1));
            sensor(key, name, "power", "W", false);
        }
        sensor("total_power_w", "Total power", "power", "W", false);
        sensor("import_kwh", "Import energy", "energy", "kWh", true);
        sensor("export_kwh", "Export energy", "energy", "kWh", true);
        sensor("sample_age_ms", "Measurement age", "duration", "ms", false, true);
        sensor("failed_reads", "Failed reads", nullptr, nullptr, false, true);
        return root;
    }
}

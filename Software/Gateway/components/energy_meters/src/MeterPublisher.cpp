#include "flexbms/MeterPublisher.h"
#include "flexbms/MeterJson.h"

namespace FlexBms::EnergyMeters
{
    bool Publisher::publishMeterJson(const char *topic, cJSON *json, bool retained)
    {
        char *payload = cJSON_PrintUnformatted(json);
        const bool sent = payload != nullptr && publishMeter(topic, payload, retained);
        cJSON_free(payload); cJSON_Delete(json); return sent;
    }
    void Publisher::tick(const State &meters, int64_t now, bool force, const char *gatewayId, const char *baseTopic, Publish send)
    {
        publishMeter = send;
        if (force || meterGeneration != meters.generation)
        {
            bool sent = true;
            for (size_t slot = 0; slot < kMeterCount; ++slot)
            {
                char topic[160]{};
                std::snprintf(topic, sizeof(topic), "homeassistant/device/%s_meter%u/config", gatewayId, static_cast<unsigned>(slot + 1));
                const bool discovered = meters.configuration.meters[slot].enabled ?
                    publishMeterJson(topic, discoveryJson(meters, slot, gatewayId, baseTopic), true) :
                    publishMeter(topic, "", true); // remove previously discovered disabled slots
                sent = discovered && sent;
            }
            // Retry reconciliation next tick if the outbox is full.
            meterGeneration = sent ? meters.generation : 0;
            meterElectricalUs = {}; meterEnergyUs = {};
            meterOnline = {-1, -1}; meterEnergyOnline = {-1, -1};
            if (!sent) return;
        }
        for (size_t slot = 0; slot < kMeterCount; ++slot)
        {
            const auto &config = meters.configuration.meters[slot];
            const auto &reading = meters.readings[slot];
            const bool online = config.enabled && meters.busReady && fresh(reading.electricalValid, reading.electricalUs, now, kElectricalFreshUs);
            const bool energyOnline = online && fresh(reading.energyValid, reading.energyUs, now, kEnergyFreshUs);
            char topic[160]{};
            std::snprintf(topic, sizeof(topic), "%s/meter%u/availability", baseTopic, static_cast<unsigned>(slot + 1));
            if (meterOnline[slot] != static_cast<int>(online) && publishMeter(topic, online ? "online" : "offline", true)) meterOnline[slot] = online;
            std::snprintf(topic, sizeof(topic), "%s/meter%u/energy_availability", baseTopic, static_cast<unsigned>(slot + 1));
            if (meterEnergyOnline[slot] != static_cast<int>(energyOnline) && publishMeter(topic, energyOnline ? "online" : "offline", true)) meterEnergyOnline[slot] = energyOnline;
            if (online && reading.electricalUs != meterElectricalUs[slot])
            {
                std::snprintf(topic, sizeof(topic), "%s/meter%u/state", baseTopic, static_cast<unsigned>(slot + 1));
                if (publishMeterJson(topic, electricalJson(reading, config.reversePowerDirection, now), false)) meterElectricalUs[slot] = reading.electricalUs;
            }
            if (energyOnline && reading.energyUs != meterEnergyUs[slot])
            {
                std::snprintf(topic, sizeof(topic), "%s/meter%u/energy", baseTopic, static_cast<unsigned>(slot + 1));
                if (publishMeterJson(topic, energyJson(reading, config.reversePowerDirection), false)) meterEnergyUs[slot] = reading.energyUs;
            }
        }
    }

}

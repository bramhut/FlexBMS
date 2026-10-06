#pragma once
#include "flexbms/MeterModel.h"
#include "cJSON.h"

namespace FlexBms::EnergyMeters
{
    // Transport-independent publisher, also exercised by native tests.
    class Publisher
    {
    public:
        using Publish = bool (*)(const char *topic, const char *payload, bool retained);
        void tick(const State &meters, int64_t nowUs, bool rediscover, const char *gatewayId, const char *baseTopic, Publish send);
    private:
        bool publishMeterJson(const char *topic, cJSON *json, bool retained);
        Publish publishMeter = nullptr;
        uint32_t meterGeneration = 0;
        std::array<int64_t, kMeterCount> meterElectricalUs{}, meterEnergyUs{};
        std::array<int, kMeterCount> meterOnline{-1, -1}, meterEnergyOnline{-1, -1};
    };
}

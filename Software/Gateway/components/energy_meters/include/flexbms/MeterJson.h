#pragma once
#include "flexbms/MeterModel.h"
#include "cJSON.h"

namespace FlexBms::EnergyMeters
{
    bool parseConfiguration(cJSON *json, Configuration &configuration);
    cJSON *configurationJson(const Configuration &configuration);
    cJSON *statusJson(const State &state, int64_t nowUs);
    cJSON *discoveryJson(const State &state, size_t slot, const char *gatewayId, const char *baseTopic);
    cJSON *electricalJson(const Reading &reading, bool reverse, int64_t nowUs);
    cJSON *energyJson(const Reading &reading, bool reverse);
}

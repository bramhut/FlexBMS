#pragma once

#include <cstddef>
#include <cstdint>

namespace FlexBms::GatewayAssets
{
    struct Asset { const char *path; const char *mime; const uint8_t *data; size_t bytes; };
    extern const Asset kAssets[];
    extern const size_t kAssetCount;
    extern const char kCompanionVersion[];
    extern const char kCompanionBuildId[];
}

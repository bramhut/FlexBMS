#pragma once

#include <algorithm>
#include <cstdint>

namespace BatterySoc
{
    // The retained BCC raw value spans -100% to 200%. GoodWe takes an integer
    // percentage and stops charging at 100%; reserve that value for a
    // completed full-charge calibration.
    constexpr uint16_t inverterPercent(uint16_t rawSoc, bool fullChargeConfirmed)
    {
        const int32_t roundedPercent =
            (static_cast<int32_t>(rawSoc) * 300 + 32767) / 65535 - 100;
        return static_cast<uint16_t>(std::clamp<int32_t>(
            roundedPercent, 0, fullChargeConfirmed ? 100 : 99));
    }
}

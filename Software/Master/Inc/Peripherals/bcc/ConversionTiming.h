#pragma once
#include <algorithm>
#include <cstdint>

namespace BccConversionTiming
{
    constexpr uint32_t elapsedSinceLatestStartUs(uint32_t nowUs, uint32_t localStartUs,
                                                 uint32_t globalStartUs)
    {
        // Compare ages, not raw timestamps: micros() wraps every ~71.6 minutes.
        // At least one start belongs to the conversion currently being awaited.
        return std::min(nowUs - localStartUs, nowUs - globalStartUs);
    }

    constexpr uint32_t remainingWaitUs(uint32_t elapsedUs, uint32_t conversionUs)
    {
        return elapsedUs < conversionUs ? conversionUs - elapsedUs : 0U;
    }
}

#include "Peripherals/bcc/ConversionTiming.h"
#include <cassert>
#include <cstdint>

int main()
{
    using BccConversionTiming::elapsedSinceLatestStartUs;
    using BccConversionTiming::remainingWaitUs;
    constexpr uint32_t conversionUs = 4800U;

    // Normal global acquisition with diagnostics disabled or a retained local start.
    assert(elapsedSinceLatestStartUs(105000U, 0U, 101000U) == 4000U);
    assert(elapsedSinceLatestStartUs(105000U, 10000U, 101000U) == 4000U);
    // A diagnostic starts a newer conversion on this slave only.
    assert(elapsedSinceLatestStartUs(105000U, 104000U, 101000U) == 1000U);
    assert(remainingWaitUs(0U, conversionUs) == conversionUs);
    assert(remainingWaitUs(4000U, conversionUs) == 800U);
    assert(remainingWaitUs(conversionUs, conversionUs) == 0U);
    assert(remainingWaitUs(conversionUs + 1U, conversionUs) == 0U);
    assert(remainingWaitUs(UINT32_MAX, conversionUs) == 0U);

    // The retained diagnostic timestamp looks numerically newer after rollover.
    const uint32_t elapsedUs = elapsedSinceLatestStartUs(5000U, 2000000U, 1000U);
    assert(elapsedUs == 4000U);
    assert(remainingWaitUs(elapsedUs, conversionUs) == 800U);
    assert(elapsedSinceLatestStartUs(5000U, 0U, 1000U) == 4000U);
    // Local diagnostics after rollover, and conversions that themselves span it.
    assert(elapsedSinceLatestStartUs(5000U, 1000U, 0xFFFFF000U) == 4000U);
    assert(elapsedSinceLatestStartUs(1000U, 2000000U, UINT32_MAX - 999U) == 2000U);
    assert(elapsedSinceLatestStartUs(1000U, UINT32_MAX - 999U, 2000000U) == 2000U);

    // Exercise each averaging duration across the wrap and signed-half-range boundary.
    // The stale alternative may be more than 2^31 us old; ages remain unsigned.
    for (uint32_t durationUs : {600U, 4800U, 153600U})
    {
        for (uint32_t nowUs : {0U, 5000U, 0x7FFFFFFFU, 0x80000000U, UINT32_MAX})
        {
            for (uint32_t ageUs : {0U, 1U, durationUs - 1U, durationUs, durationUs + 1U})
            {
                const uint32_t startUs = nowUs - ageUs;
                const uint32_t staleUs = nowUs - 0x80010000U;
                const uint32_t expectedWaitUs = ageUs < durationUs ? durationUs - ageUs : 0U;
                for (bool localLatest : {false, true})
                {
                    const uint32_t age = elapsedSinceLatestStartUs(
                        nowUs, localLatest ? startUs : staleUs, localLatest ? staleUs : startUs);
                    assert(age == ageUs);
                    assert(remainingWaitUs(age, durationUs) == expectedWaitUs);
                    assert(remainingWaitUs(age, durationUs) <= durationUs);
                }
            }
        }
    }
}

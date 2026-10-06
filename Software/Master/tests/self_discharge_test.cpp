#include "Peripherals/RuntimeConfiguration.h"
#include "Peripherals/SelfDischarge.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>

int main()
{
    constexpr uint32_t capacityMilliAh = 314'000U;
    constexpr uint8_t onePercentRate = 10U;
    constexpr uint32_t thirtyDaysMs = 30U * 24U * 60U * 60U * 1'000U;

    SelfDischarge::Calculator monthly;
    assert(monthly.advance(capacityMilliAh, onePercentRate, thirtyDaysMs) == 3'140'000U);
    assert(monthly.remainder() == 0U);
    assert(SelfDischarge::equivalentCurrentMicroAmps(capacityMilliAh, onePercentRate) == 4'361U);

    // Carrying the numerator remainder makes any partition of the same powered
    // time produce exactly the same whole-µAh result.
    constexpr uint64_t totalDurationMs = 400ULL * 24U * 60U * 60U * 1'000U;
    SelfDischarge::Calculator wholeDuration;
    assert(totalDurationMs > UINT32_MAX);
    uint64_t expected = 0U;
    uint64_t expectedRemaining = totalDurationMs;
    while (expectedRemaining != 0U)
    {
        const uint32_t duration = static_cast<uint32_t>(
            expectedRemaining > UINT32_MAX ? UINT32_MAX : expectedRemaining);
        expected += wholeDuration.advance(capacityMilliAh, onePercentRate, duration);
        expectedRemaining -= duration;
    }
    SelfDischarge::Calculator segmented;
    uint64_t segmentedLoss = 0U;
    uint64_t remaining = totalDurationMs;
    const uint32_t chunkSizes[] = {1U, 19U, 997U, 60'001U, 86'400'000U};
    size_t chunk = 0U;
    while (remaining != 0U)
    {
        const uint32_t duration = static_cast<uint32_t>(
            remaining < chunkSizes[chunk % (sizeof(chunkSizes) / sizeof(chunkSizes[0]))]
                ? remaining
                : chunkSizes[chunk % (sizeof(chunkSizes) / sizeof(chunkSizes[0]))]);
        segmentedLoss += segmented.advance(capacityMilliAh, onePercentRate, duration);
        remaining -= duration;
        ++chunk;
    }
    assert(segmentedLoss == expected);
    assert(segmented.remainder() == wholeDuration.remainder());

    // Validate the largest accepted single interval and its plus-remainder sum.
    SelfDischarge::Calculator maximumInterval;
    const uint64_t maximumNumerator = static_cast<uint64_t>(10'000'000U) * 50U * UINT32_MAX;
    assert(maximumInterval.advance(10'000'000U, 50U, UINT32_MAX) ==
           maximumNumerator / SelfDischarge::kDenominator);
    assert(maximumInterval.remainder() == maximumNumerator % SelfDischarge::kDenominator);

    // millis() subtraction correctly accounts for its uint32 wrap.
    const uint32_t beforeWrap = UINT32_MAX - 99U;
    const uint32_t afterWrap = 100U;
    const uint32_t elapsedAcrossWrap = afterWrap - beforeWrap;
    assert(elapsedAcrossWrap == 200U);
    SelfDischarge::Calculator wrapCalculator;
    SelfDischarge::Calculator directCalculator;
    assert(wrapCalculator.advance(capacityMilliAh, onePercentRate, elapsedAcrossWrap) ==
           directCalculator.advance(capacityMilliAh, onePercentRate, 200U));
    assert(wrapCalculator.remainder() == directCalculator.remainder());

    // Rate changes affect only explicitly supplied powered time; a new firmware
    // session starts with no prior remainder, so no powered-off catch-up occurs.
    SelfDischarge::Calculator changedRate;
    const uint64_t firstRate = changedRate.advance(capacityMilliAh, 10U, 12'345U);
    const uint64_t secondRate = changedRate.advance(capacityMilliAh, 20U, 54'321U);
    const uint64_t combinedNumerator = static_cast<uint64_t>(capacityMilliAh) *
                                       (10U * 12'345U + 20U * 54'321U);
    assert(firstRate + secondRate == combinedNumerator / SelfDischarge::kDenominator);
    SelfDischarge::Calculator afterRestart;
    assert(afterRestart.advance(capacityMilliAh, 20U, 54'321U) ==
           static_cast<uint64_t>(capacityMilliAh) * 20U * 54'321U /
               SelfDischarge::kDenominator);

    SelfDischarge::Calculator disabled;
    assert(disabled.advance(capacityMilliAh, 0U, UINT32_MAX) == 0U);
    assert(disabled.remainder() == 0U);

    // Calibration starts a fresh complete diagnostic interval; restart starts
    // a zeroed, incomplete RAM interval and invalid SoC never integrates loss.
    SelfDischarge::CalibrationInterval interval;
    assert(!interval.complete());
    assert(interval.advance(capacityMilliAh, onePercentRate, thirtyDaysMs, false) == 0U);
    assert(!interval.complete());
    interval.beginAfterCalibration(true);
    assert(interval.complete());
    assert(interval.advance(capacityMilliAh, onePercentRate, thirtyDaysMs, true) == 3'140'000U);
    assert(interval.accumulatedMicroAh() == 3'140'000U);
    interval.beginAfterCalibration(true);
    assert(interval.accumulatedMicroAh() == 0U);
    assert(interval.complete());
    interval.advance(capacityMilliAh, onePercentRate, 1'000U, false);
    assert(!interval.complete());
    SelfDischarge::CalibrationInterval afterStm32Restart;
    assert(afterStm32Restart.accumulatedMicroAh() == 0U);
    assert(!afterStm32Restart.complete());

    assert(RuntimeConfiguration::selfDischargeRateForLegacyWrite(
               RuntimeConfiguration::LoadStatus::Valid, 25U) == 25U);
    assert(RuntimeConfiguration::selfDischargeRateForLegacyWrite(
               RuntimeConfiguration::LoadStatus::Blank, 25U) == 0U);
    assert(RuntimeConfiguration::selfDischargeRateForLegacyWrite(
               RuntimeConfiguration::LoadStatus::VersionMismatch, 25U) == 0U);
    assert(SelfDischarge::roundedMilliAmpHours(UINT64_MAX) >=
           SelfDischarge::roundedMilliAmpHours(UINT64_MAX - 1U));
    assert(SelfDischarge::roundedMilliAmpHours(UINT64_MAX) ==
           UINT64_MAX / 1'000U + (UINT64_MAX % 1'000U >= 500U ? 1U : 0U));

    // Exercise the actual double-counter update interface for one year at the
    // suggested 314 Ah / 1% setting. The fixed-point loss remains authoritative;
    // this bounds only conversion/subtraction rounding in the retained Ah value.
    SelfDischarge::Calculator annual;
    double retainedAh = 314.0;
    uint64_t exactAnnualLossMicroAh = 0U;
    for (uint32_t second = 0U; second < 365U * 24U * 60U * 60U; ++second)
    {
        const uint64_t emittedMicroAh = annual.advance(capacityMilliAh, onePercentRate, 1'000U);
        exactAnnualLossMicroAh += emittedMicroAh;
        if (emittedMicroAh != 0U)
        {
            retainedAh = std::clamp(retainedAh - static_cast<double>(emittedMicroAh) / 1'000'000.0,
                                    0.0, 628.0);
        }
    }
    const double exactRetainedAh = 314.0 - static_cast<double>(exactAnnualLossMicroAh) / 1'000'000.0;
    const double interfaceErrorMicroAh = std::abs(retainedAh - exactRetainedAh) * 1'000'000.0;
    assert(interfaceErrorMicroAh < 10.0);
    return 0;
}

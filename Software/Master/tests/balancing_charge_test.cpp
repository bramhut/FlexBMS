#include "Peripherals/BalancingCharge.h"

#include <cassert>
#include <cstdint>
#include <limits>

int main()
{
    using BalancingCharge::Timeline;

    // A confirmed cell at 3.45 V through the existing 22.8 ohm path removes
    // approximately 151 mAh in one hour.
    BalancingCharge::Counter counter{};
    const uint64_t oneHour = BalancingCharge::milliAmpMilliseconds(3'450'000U, 22.8, 3'600'000U);
    BalancingCharge::addMilliAmpMilliseconds(counter, oneHour);
    assert(counter.milliAmpHours == 151U);
    assert(counter.remainderMilliAmpMilliseconds > 0U);

    const auto beforeZeroIncrement = counter;
    BalancingCharge::addMilliAmpMilliseconds(counter, 0U);
    assert(counter.milliAmpHours == beforeZeroIncrement.milliAmpHours);
    assert(counter.remainderMilliAmpMilliseconds == beforeZeroIncrement.remainderMilliAmpMilliseconds);

    // Each enabled window accrues; measurement pauses and known disabled time do not.
    Timeline timeline;
    timeline.start(100U, 1'000U);
    assert(timeline.takeEnabledMilliseconds(150U) == 50U);
    timeline.pause(150U);
    assert(timeline.takeEnabledMilliseconds(400U) == 0U);
    timeline.resume(500U);
    assert(timeline.takeEnabledMilliseconds(550U) == 50U);
    timeline.stop(550U);
    assert(timeline.takeEnabledMilliseconds(700U) == 0U);

    // Never count beyond the monitor's configured pulse timeout.
    timeline.start(1'000U, 200U);
    assert(timeline.takeEnabledMilliseconds(1'250U) == 200U);
    assert(timeline.takeEnabledMilliseconds(1'300U) == 0U);

    // Deadline and elapsed time remain correct when the HAL millisecond tick wraps.
    timeline.start(std::numeric_limits<uint32_t>::max() - 99U, 200U);
    assert(timeline.takeEnabledMilliseconds(50U) == 150U);
    assert(timeline.takeEnabledMilliseconds(150U) == 50U);
    assert(timeline.takeEnabledMilliseconds(200U) == 0U);

    // An uncertain command state stops tracking until a new confirmed pulse.
    timeline.start(10U, 1'000U);
    timeline.invalidate(20U);
    assert(timeline.takeEnabledMilliseconds(500U) == 0U);
    timeline.reset();
    assert(timeline.takeEnabledMilliseconds(600U) == 0U);
    counter = {};
    assert(counter.milliAmpHours == 0U && counter.remainderMilliAmpMilliseconds == 0U);
    return 0;
}

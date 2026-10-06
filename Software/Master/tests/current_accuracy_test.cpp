#include "Peripherals/CoulombCounterDelta.h"
#include "Peripherals/CurrentUnits.h"
#include "Peripherals/EnergyCounter.h"
#include <cassert>
#include <limits>
#include <random>

int main() {
    using namespace CoulombCounterDelta;
    Tracker tracker;
    assert(tracker.sample(65530, 0x7fffffff, UINT32_MAX - 49999).result == Result::Baseline);
    auto delta = tracker.sample(994, 0x800003e7, 50000);
    assert(delta.result == Result::Valid && delta.samples == 1000 && delta.accumulator == 1000 && delta.elapsedUs == 100000);
    assert(tracker.sample(994, 0x800003e7, 50000).result == Result::Duplicate);
    delta = tracker.sample(1994, 0x7fffffff, 150000);
    assert(delta.result == Result::Valid && delta.accumulator == -1000);
    assert(tracker.sample(1995, 1, 150000).result == Result::Discontinuity);
    assert(tracker.sample(1996, 2, 2150000).result == Result::Discontinuity);
    assert(tracker.sample(1, 0, 2200000).result == Result::Discontinuity);
    tracker.reset();
    assert(tracker.sample(123, 12345, 99999).result == Result::Baseline);
    assert(tracker.sample(8124, 12346, 199999).result == Result::Discontinuity);
    assert(CurrentUnits::microAmps(0.0000006) == 1);
    assert(CurrentUnits::microAmps(-0.0000006) == -1);
    assert(CurrentUnits::microAmps(3000) == INT32_MAX);
    assert(CurrentUnits::microAmps(-3000) == INT32_MIN);
    assert(CurrentUnits::microAmps(std::numeric_limits<double>::quiet_NaN()) == 0);
    std::mt19937 rng(42);
    for (int i = 0; i < 10000; ++i) {
        const uint32_t v = rng(), a = rng(), us = rng();
        uint64_t remainder = rng(), expectedRemainder = remainder;
        const unsigned __int128 numerator = static_cast<unsigned __int128>(v) * a * us + expectedRemainder;
        const auto actual = EnergyCounter::integrateMicroWh(0, remainder, v, a, us);
        assert(actual == numerator / EnergyCounter::kMicroWhDenominator);
        assert(remainder == numerator % EnergyCounter::kMicroWhDenominator);
    }
    uint64_t remainder = 0, total = 0;
    for (int i = 0; i < 86400; ++i) total = EnergyCounter::integrateMicroWh(total, remainder, 324000000, 5000, 1000000);
    assert(total == 38880000 && remainder == 0); // 38.88 Wh at 5 mA for 24 h
    uint64_t splitR = 0, singleR = 0, split = 0;
    for (int i = 0; i < 1000; ++i) split = EnergyCounter::integrateMicroWh(split, splitR, 323456789, 1, 12345);
    assert(split == EnergyCounter::integrateMicroWh(0, singleR, 323456789, 1, 12345000));
    assert(splitR == singleR);
}

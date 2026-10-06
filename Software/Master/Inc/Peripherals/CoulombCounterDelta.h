#pragma once
#include <cstdint>
namespace CoulombCounterDelta {
enum class Result { Baseline, Duplicate, Valid, Discontinuity };
struct Delta { Result result; int64_t accumulator = 0; uint16_t samples = 0; uint32_t elapsedUs = 0; };
class Tracker {
public:
    void reset() { initialized = false; }
    Delta sample(uint16_t samples, uint32_t accumulator, uint32_t time) {
        if (!initialized) { baseline(samples, accumulator, time); return {Result::Baseline}; }
        const uint32_t elapsed = time - previousTime;
        const uint16_t count = static_cast<uint16_t>(samples - previousSamples);
        const uint32_t bits = accumulator - previousAccumulator;
        const int64_t sum = bits <= INT32_MAX ? static_cast<int64_t>(bits) : static_cast<int64_t>(bits) - 0x100000000LL;
        if (elapsed == 0 && count == 0 && bits == 0) return {Result::Duplicate};
        // 0.6 uV/LSB, +/-150 mV: <=250000 counts/sample. At <=8000
        // samples the accumulator delta fits int32. At 16-bit ADC resolution
        // a <=1 s interval cannot conceal a full 65536-sample wrap.
        if (elapsed == 0 || elapsed > 1000000U || count == 0 || count > 8000U ||
            sum > int64_t(count) * 250000 || sum < -int64_t(count) * 250000) {
            baseline(samples, accumulator, time); return {Result::Discontinuity};
        }
        baseline(samples, accumulator, time);
        return {Result::Valid, sum, count, elapsed};
    }
private:
    void baseline(uint16_t samples, uint32_t accumulator, uint32_t time) {
        previousSamples = samples; previousAccumulator = accumulator; previousTime = time; initialized = true;
    }
    bool initialized = false;
    uint16_t previousSamples = 0;
    uint32_t previousAccumulator = 0, previousTime = 0;
};
}

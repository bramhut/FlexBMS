#pragma once

#include <cmath>
#include <cstdint>

namespace BalancingCharge
{
    constexpr uint64_t kMilliAmpMillisecondsPerMilliAmpHour = 3'600'000ULL;

    struct Counter
    {
        uint32_t milliAmpHours{};
        uint32_t remainderMilliAmpMilliseconds{};
    };

    class Timeline
    {
    public:
        void start(uint32_t nowMs, uint32_t pulseDurationMs)
        {
            lastMs_ = nowMs;
            deadlineMs_ = nowMs + pulseDurationMs;
            active_ = true;
            paused_ = false;
            known_ = true;
        }

        void pause(uint32_t nowMs)
        {
            lastMs_ = nowMs;
            paused_ = true;
        }

        void resume(uint32_t nowMs)
        {
            lastMs_ = nowMs;
            paused_ = false;
        }

        void stop(uint32_t nowMs)
        {
            lastMs_ = nowMs;
            active_ = false;
            paused_ = true;
        }

        void invalidate(uint32_t nowMs)
        {
            lastMs_ = nowMs;
            known_ = false;
            paused_ = true;
        }

        void reset()
        {
            lastMs_ = 0U;
            deadlineMs_ = 0U;
            active_ = false;
            paused_ = true;
            known_ = false;
        }

        uint32_t takeEnabledMilliseconds(uint32_t nowMs)
        {
            if (!active_ || paused_ || !known_)
            {
                lastMs_ = nowMs;
                return 0U;
            }

            uint32_t durationMs = nowMs - lastMs_;
            if (static_cast<int32_t>(nowMs - deadlineMs_) >= 0)
            {
                durationMs = static_cast<int32_t>(lastMs_ - deadlineMs_) < 0
                                 ? deadlineMs_ - lastMs_
                                 : 0U;
                active_ = false;
                paused_ = true;
            }
            lastMs_ = nowMs;
            return durationMs;
        }

    private:
        uint32_t lastMs_{};
        uint32_t deadlineMs_{};
        bool active_{};
        bool paused_{true};
        bool known_{};
    };

    // The existing board resistance includes the bleed resistor and series
    // components. Charge is retained as mA*ms so short measurement intervals
    // do not lose sub-mAh contributions through repeated integer conversion.
    inline uint64_t milliAmpMilliseconds(uint32_t cellVoltageUv,
                                         double effectiveResistanceOhms,
                                         uint32_t enabledDurationMs)
    {
        if (cellVoltageUv == 0U || effectiveResistanceOhms <= 0.0 || enabledDurationMs == 0U)
        {
            return 0U;
        }

        const double currentMilliAmps =
            static_cast<double>(cellVoltageUv) / (1'000.0 * effectiveResistanceOhms);
        return static_cast<uint64_t>(std::llround(currentMilliAmps * enabledDurationMs));
    }

    inline void addMilliAmpMilliseconds(Counter &counter, uint64_t increment)
    {
        if (counter.milliAmpHours == UINT32_MAX) return;
        const uint64_t accumulated = counter.remainderMilliAmpMilliseconds + increment;
        const uint64_t wholeMilliAmpHours = accumulated / kMilliAmpMillisecondsPerMilliAmpHour;
        if (wholeMilliAmpHours > UINT32_MAX - counter.milliAmpHours)
        {
            counter.milliAmpHours = UINT32_MAX;
            counter.remainderMilliAmpMilliseconds = 0U;
            return;
        }
        counter.milliAmpHours += static_cast<uint32_t>(wholeMilliAmpHours);
        counter.remainderMilliAmpMilliseconds = static_cast<uint32_t>(
            accumulated % kMilliAmpMillisecondsPerMilliAmpHour);
    }
}

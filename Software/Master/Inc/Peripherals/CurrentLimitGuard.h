#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace CurrentLimitGuard
{
    inline constexpr uint32_t DwellMs = 5'000U, MaximumSampleGapMs = 1'000U;
    inline constexpr double ZeroToleranceA = 1, MinimumMarginA = 2, MarginFraction = .10;
    inline uint16_t encodeDeciAmps(double value)
    {
        return !std::isfinite(value) || value <= 0 ? 0U :
            static_cast<uint16_t>(std::clamp(std::floor(value * 10), 0.0, 65535.0));
    }
    struct Event { uint8_t direction{}; int32_t measuredMilliA{}; uint16_t limitDeciA{}; uint32_t uptimeMs{}; };
    class Timer
    {
        bool tracking = false;
        uint32_t elapsed = 0, previous = 0;
        bool previousValid = false;
    public:
        void reset() { tracking = previousValid = false; elapsed = 0; }
        bool update(bool valid, bool violation, uint32_t now)
        {
            if (!valid) { previousValid = false; return false; }
            if (!violation) { reset(); return false; }
            if (!tracking) { tracking = true; elapsed = 0; }
            else if (previousValid && now - previous <= MaximumSampleGapMs)
                elapsed += std::min<uint32_t>(now - previous, DwellMs - elapsed);
            previous = now; previousValid = true;
            return elapsed >= DwellMs;
        }
    };
    class Controller
    {
        Timer charge, discharge;
    public:
        void reset() { charge.reset(); discharge.reset(); }
        uint8_t update(bool armed, bool valid, double currentA, uint16_t chargeDeciA,
                       uint16_t dischargeDeciA, uint32_t now)
        {
            if (!armed) { reset(); return 0; }
            valid = valid && std::isfinite(currentA);
            const auto ceiling = [](uint16_t deci) {
                const double limit = deci / 10.0;
                return deci == 0 ? ZeroToleranceA : limit + std::max(MinimumMarginA, limit * MarginFraction);
            };
            const bool c = charge.update(valid, currentA > ceiling(chargeDeciA), now);
            const bool d = discharge.update(valid, -currentA > ceiling(dischargeDeciA), now);
            return c ? 1 : d ? 2 : 0;
        }
    };
}

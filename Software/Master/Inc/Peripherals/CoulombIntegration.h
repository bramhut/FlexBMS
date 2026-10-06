#pragma once
#include <algorithm>

namespace CoulombIntegration
{
    inline double correctedCharge(double chargeC, double seconds, bool invert, double consumptionA)
    {
        return (invert ? -chargeC : chargeC) - consumptionA * seconds;
    }

    // No current deadband: every valid charge increment contributes to SoC.
    inline double accumulatedAh(double previousAh, double chargeC, double capacityAh)
    {
        return std::clamp(previousAh + chargeC / 3600.0, 0.0, capacityAh * 2.0);
    }
}

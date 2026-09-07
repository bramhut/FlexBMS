#pragma once

#include <algorithm>
#include <cstdint>

namespace BatteryLimits
{
    struct Config
    {
        double chargeDeratingStartCellV;
        double chargeTargetCellV;
        double dischargeTargetCellV;
        double dischargeStopCellV;
        double recoveryFractionPerSecond;
    };

    struct Limits
    {
        double chargeVoltageV;
        double dischargeVoltageV;
        double chargeCurrentA;
        double dischargeCurrentA;
    };

    constexpr double clamp01(double value)
    {
        return std::clamp(value, 0.0, 1.0);
    }

    constexpr Limits calculate(const Config &config,
                               uint16_t cellCount,
                               double minCellV,
                               double maxCellV,
                               double maximumChargeCurrentA,
                               double maximumDischargeCurrentA)
    {
        const double chargeFraction = clamp01(
            (config.chargeTargetCellV - maxCellV) /
            (config.chargeTargetCellV - config.chargeDeratingStartCellV));
        const double dischargeFraction = clamp01(
            (minCellV - config.dischargeStopCellV) /
            (config.dischargeTargetCellV - config.dischargeStopCellV));

        return {
            .chargeVoltageV = config.chargeTargetCellV * cellCount,
            .dischargeVoltageV = config.dischargeTargetCellV * cellCount,
            .chargeCurrentA = maximumChargeCurrentA * chargeFraction,
            .dischargeCurrentA = maximumDischargeCurrentA * dischargeFraction,
        };
    }

    // A newly lower limit applies immediately. Recovery is deliberately slow
    // so measurement noise cannot step the inverter back to full current.
    constexpr double applyRecoverySlew(double previousA,
                                       double requestedA,
                                       double maximumA,
                                       double recoveryFractionPerSecond,
                                       uint32_t elapsedMs)
    {
        if (requestedA <= previousA)
        {
            return requestedA;
        }

        const double maximumIncreaseA =
            maximumA * recoveryFractionPerSecond * elapsedMs / 1000.0;
        return std::min(requestedA, previousA + maximumIncreaseA);
    }
}

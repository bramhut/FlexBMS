#include "Peripherals/BatteryLimits.h"

namespace
{
    constexpr BatteryLimits::Config config{
        .chargeDeratingStartCellV = 3.400,
        .chargeTargetCellV = 3.500,
        .dischargeTargetCellV = 2.900,
        .dischargeStopCellV = 2.800,
        .recoveryFractionPerSecond = 0.10,
    };

    constexpr bool near(double actual, double expected)
    {
        return actual > expected - 0.000001 && actual < expected + 0.000001;
    }

    constexpr bool limitsAreCorrect()
    {
        const auto unrestricted = BatteryLimits::calculate(config, 96U, 3.000, 3.400, 63.0, 63.0);
        const auto halfCharge = BatteryLimits::calculate(config, 96U, 3.000, 3.450, 63.0, 63.0);
        const auto chargeStopped = BatteryLimits::calculate(config, 96U, 3.000, 3.500, 63.0, 63.0);
        const auto halfDischarge = BatteryLimits::calculate(config, 96U, 2.850, 3.300, 63.0, 63.0);
        const auto dischargeStopped = BatteryLimits::calculate(config, 96U, 2.800, 3.300, 63.0, 63.0);

        return near(unrestricted.chargeVoltageV, 336.0) &&
               near(unrestricted.dischargeVoltageV, 278.4) &&
               near(unrestricted.chargeCurrentA, 63.0) &&
               near(halfCharge.chargeCurrentA, 31.5) &&
               near(chargeStopped.chargeCurrentA, 0.0) &&
               near(halfDischarge.dischargeCurrentA, 31.5) &&
               near(dischargeStopped.dischargeCurrentA, 0.0);
    }

    constexpr bool recoveryIsControlled()
    {
        return near(BatteryLimits::applyRecoverySlew(40.0, 20.0, 63.0, 0.10, 1000U), 20.0) &&
               near(BatteryLimits::applyRecoverySlew(20.0, 63.0, 63.0, 0.10, 1000U), 26.3) &&
               near(BatteryLimits::applyRecoverySlew(60.0, 63.0, 63.0, 0.10, 1000U), 63.0);
    }
}

static_assert(limitsAreCorrect());
static_assert(recoveryIsControlled());

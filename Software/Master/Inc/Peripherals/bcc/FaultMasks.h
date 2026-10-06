#pragma once

#include "MC33771C.h"

namespace BccFaultMasks
{
    constexpr void apply(uint16_t &fault1, uint16_t &fault2, uint16_t &fault3)
    {
        // Threshold faults have dedicated registers. EEPROM is unused and
        // communication loss is handled by the master's communication watchdog.
        constexpr uint16_t ignoredFault1 =
            MC33771C_FAULT1_STATUS_CT_UV_FLT_MASK |
            MC33771C_FAULT1_STATUS_CT_OV_FLT_MASK |
            MC33771C_FAULT1_STATUS_AN_UT_FLT_MASK |
            MC33771C_FAULT1_STATUS_AN_OT_FLT_MASK |
            MC33771C_FAULT1_STATUS_I2C_ERR_FLT_MASK |
            MC33771C_FAULT1_STATUS_COM_LOSS_FLT_MASK;
        fault1 &= static_cast<uint16_t>(~ignoredFault1);

        // Preserve ALL FAULT2 bits, especially IC_TSD_FLT: the controller
        // needs that bit to raise its dedicated latched thermal fault.
        (void)fault2;

        // Keep existing policy: retain diagnostic timeout (bit 14), ignore
        // balancing end-of-timer notifications and coulomb-counter overflow.
        fault3 &= 0x4000U;
    }
}

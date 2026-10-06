#include "Peripherals/bcc/FaultMasks.h"

constexpr bool thermalFlagSurvives()
{
    // Exhaustively check every FAULT2 combination, including TSD alone and
    // simultaneous thermal/integrity faults. Other masks must stay unchanged.
    for (unsigned raw = 0; raw <= 0xffffU; ++raw)
    {
        uint16_t f1 = 0xffffU, f2 = static_cast<uint16_t>(raw), f3 = 0xffffU;
        BccFaultMasks::apply(f1, f2, f3);
        if (f2 != raw || f3 != 0x4000U) return false;
        if ((f1 & (MC33771C_FAULT1_STATUS_AN_OT_FLT_MASK |
                   MC33771C_FAULT1_STATUS_AN_UT_FLT_MASK)) != 0U) return false;
    }
    return true;
}
static_assert(thermalFlagSurvives());

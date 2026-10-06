// Link the actual FaultManager with host-only hardware/RTOS boundaries.
#include "Peripherals/CurrentLimitGuard.h"
#include "Peripherals/FaultManager.h"
#include <cassert>
static unsigned safeOffCalls = 0;
namespace PCC { void forceSafeOffFromFaultManager() { ++safeOffCalls; } }
namespace BccBreadcrumb { void captureOnBoot() {} }
int main()
{
    using namespace FaultManager;
    setup(); setStartupComplete(true); setHvRunning(true);
    assert(canEnableHv());
    CurrentLimitGuard::Controller guard;
    for (unsigned now=0; now<=5000; now+=50)
        setBmsFault(BmsFault::InverterCurrentLimit,
                    guard.update(true, true, 45, 400, 400, now) != 0);
    const auto fault = getSnapshot();
    assert(fault.bmsActive == (1U << 11) && fault.bmsLatched == (1U << 11));
    assert(safeOffCalls == 1 && !canEnableHv() && !acknowledge());
    setBmsFault(BmsFault::InverterCurrentLimit, true);
    assert(safeOffCalls == 1);
    // After safe-off, the producer disarms. Only the active condition clears.
    setBmsFault(BmsFault::InverterCurrentLimit,
                guard.update(false, true, 0, 0, 0, 5050) != 0);
    assert(getSnapshot().bmsActive == 0 && getSnapshot().bmsLatched == (1U << 11));
    assert(!canEnableHv());
    assert(acknowledge() && canEnableHv());
    assert(!acknowledge());
}

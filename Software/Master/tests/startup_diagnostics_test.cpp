#include "Peripherals/StartupDiagnostics.h"
#include <cassert>

int main()
{
    // Every possible set of open GPIOs, for zero through seven fitted sensors.
    for (uint8_t count = 0U; count <= 7U; ++count)
    {
        for (uint16_t mask = 0U; mask < 128U; ++mask)
        {
            bool expectedFailure = false;
            for (uint8_t sensor = 0U; sensor < count; ++sensor)
                expectedFailure |= (mask & (1U << sensor)) != 0U;
            assert(StartupDiagnostics::hasOpenTemperatureInput(mask, count) == expectedFailure);
        }
    }
    assert(!StartupDiagnostics::hasOpenTemperatureInput(0x70U, 4U));
    assert(StartupDiagnostics::hasOpenTemperatureInput(0x78U, 4U));
    assert(StartupDiagnostics::temperatureInputMask(255U) == 0x7FU);
    using StartupDiagnostics::Report;
    using StartupDiagnostics::retainFirstFailure;
    Report stored{};
    retainFirstFailure(stored, {.cid = 1U, .attempt = 1U});
    assert(!stored.failed() && stored.attempt == 1U);
    retainFirstFailure(stored, {.cid = 1U, .failedChecks = 8U,
                               .trace = {.address = 0x30U, .operation = StartupDiagnostics::RegisterOperation::ReadCompleted, .value = 0x1234U},
                               .attempt = 2U, .gpioOpenMask = 0x78U, .gpioCheckedMask = 0x0FU});
    assert(stored.failed() && stored.failedChecks == 8U);
    // A subsequent all-slave timeout and even recovery preserve the first fault.
    retainFirstFailure(stored, {.cid = 1U, .status = 3U, .attempt = 3U});
    retainFirstFailure(stored, {.cid = 1U, .attempt = 4U});
    assert(stored.attempt == 2U && stored.status == 0U);
    assert(stored.trace.address == 0x30U && stored.trace.value == 0x1234U);
    assert(stored.gpioOpenMask == 0x78U && stored.gpioCheckedMask == 0x0FU);
    Report transportFailure{};
    retainFirstFailure(transportFailure, {.status = 10U, .failedDiagnostic = 0U, .attempt = 1U});
    retainFirstFailure(transportFailure, {.status = 3U, .attempt = 2U});
    assert(transportFailure.status == 10U && transportFailure.attempt == 1U);
}

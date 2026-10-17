#include "flexbms/AcquiredPublication.h"
#include <cassert>

using namespace FlexBms::Mqtt;

int main()
{
    AcquiredPublication status, pack, energy;
    assert(!status.due(0) && !pack.due(0));
    status.received(0);
    pack.received(0);
    energy.received(0);
    assert(status.due(1'000'000) && pack.due(1'000'000));
    status.published(); pack.published(); energy.published();
    assert(!status.due(1'000'001) && !pack.due(1'000'001));
    // Successful unchanged acquisition is eligible, not mere periodic polling.
    status.received(1'500'000);
    assert(status.due(2'000'000));
    assert(!pack.due(2'000'000) && !energy.due(2'000'000));
    // Coalescing selects the latest frame; a failed send does not consume it.
    pack.received(2'000'000); pack.received(2'500'000);
    assert(pack.due(3'000'000));
    assert(pack.due(3'000'001));
    pack.published();
    assert(!pack.due(3'000'002));
    // Reconnect, UART loss or invalid measurements discards pending data.
    pack.received(4'000'000); pack.reset();
    assert(!pack.due(4'000'001) && !pack.fresh(4'000'001));
    status.received(4'500'000);
    assert(!pack.due(5'000'000)); // STATUS alone cannot resurrect PACK.
    pack.received(5'000'000);
    assert(!pack.due(4'999'999)); // Clock moved backwards.
    assert(pack.due(7'999'999));
    assert(!pack.due(8'000'000)); // Exactly the age bound is rejected.
    assert(componentSource("run_request") == StateSource::Status);
    assert(componentSource("hv_running") == StateSource::Status);
    assert(componentSource("fault_active") == StateSource::Status);
    assert(componentSource("pack_power") == StateSource::Pack);
    assert(componentSource("state_of_charge") == StateSource::Pack);
    assert(componentSource("battery_energy_charged") == StateSource::Energy);
    assert(componentSource("uart_healthy") == StateSource::Diagnostic);
    assert(componentSource("gateway_uptime") == StateSource::Diagnostic);
}

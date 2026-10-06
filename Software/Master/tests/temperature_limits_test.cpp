#include "Peripherals/TemperatureLimits.h"
#include "Peripherals/CurrentLimitGuard.h"
#include "Peripherals/BatteryLimits.h"
#include <cassert>
#include <limits>

int main()
{
    TemperatureLimits::Controller t;
    const auto near = [](double a, double b) { return std::abs(a-b) < 1e-5; };
    assert(t.update(true, 5, 20, 3, 314).chargeA == 0); // cold reboot cannot bypass hysteresis
    assert(t.update(true, 5.01, 20, 3, 314).chargeA > 0);
    assert(near(t.update(true, 8, 20, 3, 314).chargeA, 37.68));
    assert(near(t.update(true, 13, 20, 3, 314).chargeA, 94.2));
    assert(near(t.update(true, 18, 20, 3, 314).chargeA, 157));
    assert(near(t.update(true, 8.205237084217977, 20, 3, 314).chargeA, 40));
    assert(near(t.update(true, 7, 20, 3, 314).chargeA, 33.284));
    assert(t.update(true, 3, 20, 3, 314).chargeA == 0);
    assert(t.update(true, 4, 20, 3, 314).chargeA == 0);
    assert(t.update(true, 5.01, 20, 3, 314).chargeA > 0);
    assert(near(t.update(true, 20, 50, 3, 314).dischargeA, 157));
    assert(near(t.update(true, 20, 53.72611464968153, 3, 314).dischargeA, 40));
    assert(t.update(true, 20, 55, 3, 314).dischargeA == 0);
    assert(t.update(true, 20, 53, 3, 314).chargeA == 0);
    assert(t.update(true, 20, 52.99, 3, 314).chargeA > 0);
    auto mixed = t.update(true, -5, 52, 3, 314);
    assert(mixed.chargeA == 0 && near(mixed.dischargeA, 94.2));
    assert(t.update(false, 20, 20, 3, 314).chargeA == 0);
    assert(t.update(true, 20, 54, 3, 314).dischargeA == 0); // invalid sample resets hot recovery
    assert(t.update(true, 20, 20, 3, 628).chargeA == 314);
    assert(t.update(true, 20, 20, 11, 314).chargeA == 0);
    assert(t.update(true, NAN, 20, 3, 314).dischargeA == 0);
    assert(t.update(true, 0, 20, 0, 314).chargeA == 0);
    assert(t.update(true, 13, 20, 10, 314).chargeA > 0);
    assert(near(t.update(true, .001, 20, 0, 314).chargeA, (.05 + .001*.014)*314));
    assert(t.update(true, 21, 20, 3, 314).chargeA == 0);
    assert(t.update(true, 20, 20, 3, 0).dischargeA == 0);
    assert(t.update(true, 20, 20, -1, 314).chargeA == 0);
    assert(t.update(true, 20, INFINITY, 3, 314).dischargeA == 0);

    CurrentLimitGuard::Controller g;
    assert(CurrentLimitGuard::encodeDeciAmps(3.29) == 32);
    assert(CurrentLimitGuard::encodeDeciAmps(NAN) == 0);
    assert(g.update(false, true, 40, 0, 0, 0) == 0);
    assert(g.update(true, true, 33, 300, 300, 0) == 0); // equality is compliant
    assert(g.update(true, true, 33.1, 300, 300, 1) == 0);
    for (uint32_t ms=1001; ms<5001; ms+=1000) assert(g.update(true,true,34,290,300,ms)==0);
    assert(g.update(true,true,34,290,300,5001)==1); // changing limits do not reset dwell
    g.reset();
    assert(g.update(true,true,1,0,0,0)==0);
    assert(g.update(true,true,1.01,0,0,1)==0);
    assert(g.update(true,false,0,0,0,4001)==0);
    assert(g.update(true,true,1.01,0,0,8001)==0); // missing data does not count
    assert(g.update(true,true,0,0,0,9001)==0); // compliance resets
    assert(g.update(true,true,-12.01,0,100,10000)==0);
    for (uint32_t ms=11000; ms<15000; ms+=1000) assert(g.update(true,true,-12.01,0,100,ms)==0);
    assert(g.update(true,true,-12.01,0,100,15000)==2);
    g.reset();
    const uint32_t start=UINT32_MAX-2000;
    for (uint32_t dt=0; dt<5000; dt+=1000) assert(g.update(true,true,2,0,0,start+dt)==0);
    assert(g.update(true,true,2,0,0,start+5000)==1);
    g.reset();
    assert(g.update(true,true,5,10,10,0)==0);
    assert(g.update(true,true,5,0,10,1000)==0); // nonzero to zero does not restart dwell
    assert(g.update(true,true,-5,0,10,2000)==0); // direction change resets charge dwell
    assert(g.update(true,true,5,0,10,3000)==0);
    assert(g.update(true,true,5,0,10,5000)==0); // unobserved long gap adds no time
    for (uint32_t ms=6000; ms<10000; ms+=1000) assert(g.update(true,true,5,0,10,ms)==0);
    assert(g.update(true,true,5,0,10,10000)==1);
    assert(BatteryLimits::applyRecoverySlew(40, 10, 63, .1, 50)==10);
    assert(near(BatteryLimits::applyRecoverySlew(0, 40, 63, .1, 1000),6.3));
}

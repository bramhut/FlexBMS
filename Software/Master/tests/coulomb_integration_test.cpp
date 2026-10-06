#include "Peripherals/CoulombIntegration.h"
#include <cassert>
#include <cmath>

static void near(double actual, double expected) { assert(std::abs(actual - expected) < 1e-8); }
static double day(double current, double consumption = 0.0, bool invert = false)
{
    double ah = 100.0;
    for (int second = 0; second < 86400; ++second)
        ah = CoulombIntegration::accumulatedAh(ah,
            CoulombIntegration::correctedCharge(current, 1.0, invert, consumption), 314.0);
    return ah;
}
int main()
{
    near(day(0.005), 100.12);
    near(day(-0.005), 99.88);
    near(day(0, 0.009), 99.784);
    near(day(0.005, 0, true), 99.88);
    near(day(0), 100);
    double ah = 100;
    for (int i = 0; i < 86400; ++i)
        ah = CoulombIntegration::accumulatedAh(ah, i % 2 ? -0.005 : 0.005, 314);
    near(ah, 100);
    near(day(0.0149), 100 + 0.0149 * 24);
    near(day(0.0151), 100 + 0.0151 * 24);
    near(CoulombIntegration::accumulatedAh(0, -1, 314), 0);
    near(CoulombIntegration::accumulatedAh(628, 1, 314), 628);
    near(CoulombIntegration::accumulatedAh(314, 3600, 314), 315);
    // Splitting an interval (including a restored retained Ah value) does not
    // drop charge or require a new software accumulation/restart state.
    near(CoulombIntegration::accumulatedAh(100.06, 0.005 * 43200, 314), day(0.005));
}

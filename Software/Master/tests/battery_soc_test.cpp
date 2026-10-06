#include "Peripherals/BatterySoc.h"

static_assert(BatterySoc::inverterPercent(21845U, false) == 0U);
static_assert(BatterySoc::inverterPercent(43689U, false) == 99U);
static_assert(BatterySoc::inverterPercent(43690U, false) == 99U);
static_assert(BatterySoc::inverterPercent(65535U, false) == 99U);
static_assert(BatterySoc::inverterPercent(43690U, true) == 100U);
static_assert(BatterySoc::inverterPercent(65535U, true) == 100U);
static_assert(BatterySoc::inverterPercent(43472U, true) == 99U);

int main() { return 0; }

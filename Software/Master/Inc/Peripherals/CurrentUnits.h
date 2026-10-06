#pragma once
#include <cmath>
#include <cstdint>
namespace CurrentUnits {
inline int32_t microAmps(double amps) {
    if (!std::isfinite(amps)) return 0;
    const double value = amps * 1e6;
    if (value >= INT32_MAX) return INT32_MAX;
    if (value <= INT32_MIN) return INT32_MIN;
    return static_cast<int32_t>(std::round(value));
}
}

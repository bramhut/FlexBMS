#pragma once

#include <cstdint>

namespace RtcTime
{
    // The STM32 calendar stores years 00 through 99, represented here as
    // 2000 through 2099 UTC.  The UART contract remains a uint32 Unix time.
    bool isSupportedUnixTime(uint32_t unixTime);
    bool setUnixTime(uint32_t unixTime);
    bool getUnixTime(uint32_t &unixTime);
}

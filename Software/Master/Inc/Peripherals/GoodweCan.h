#pragma once

#include "CAN.h"

#include <cstdint>

namespace GoodweCan
{
    struct FrameDiagnostics
    {
        uint32_t count{};
        uint32_t lastSeenMs{};
        uint8_t length{};
        uint8_t data[8]{};
    };

    struct Diagnostics
    {
        FrameDiagnostics timeout420{};
        FrameDiagnostics inverter425{};
        FrameDiagnostics inverter305{};
        uint32_t transmitFailures{};
    };

    /*! @brief Configure the selected bus, install passive RX monitoring, and prepare the module. */
    bool setup(CAN *can);

    /*! @brief Start the periodic transmitter after SlaveController setup. */
    bool start();

    /*! @brief Copy receive/transmit diagnostics for service tooling. */
    Diagnostics getDiagnostics();
}


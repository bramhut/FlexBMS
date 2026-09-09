#pragma once

#include "CAN.h"

#include <array>
#include <cstdint>

namespace GoodweCan
{
    inline constexpr std::array<uint32_t, 7U> TRANSMIT_FRAME_IDS = {
        0x453U, 0x455U, 0x456U, 0x457U, 0x458U, 0x45AU, 0x460U};

    inline constexpr std::array<uint32_t, 3U> RECEIVE_FRAME_IDS = {
        0x420U, 0x425U, 0x305U};

    struct FrameDiagnostics
    {
        uint32_t count{};
        uint32_t lastSeenMs{};
        uint8_t length{};
        uint8_t data[8]{};
    };

    struct TransmitFrameDiagnostics
    {
        uint32_t successCount{};
        uint32_t lastSuccessMs{};
    };

    struct Diagnostics
    {
        FrameDiagnostics timeout420{};
        FrameDiagnostics inverter425{};
        FrameDiagnostics inverter305{};
        std::array<TransmitFrameDiagnostics, TRANSMIT_FRAME_IDS.size()> transmitFrames{};
        uint32_t transmitCycles{};
        uint32_t snapshotUnavailableCycles{};
        uint32_t transmitFailures{};
        uint32_t lastTransmitFailureMs{};
        uint32_t lastTransmitFailureId{};
        uint16_t last458VoltageDeciV{};
        int16_t last458CurrentDeciA{};
        uint8_t transmitErrorCount{};
        uint8_t receiveErrorCount{};
        uint8_t errorLoggingCount{};
        uint8_t protocolLastErrorCode{};
        uint8_t protocolActivity{};
        bool errorPassive{};
        bool warning{};
        bool busOff{};
        uint32_t halErrorCode{};
        uint32_t transmitFifoFreeLevel{};
    };

    /*! @brief Configure the selected bus, install passive RX monitoring, and prepare the module. */
    bool setup(CAN *can);

    /*! @brief Start the periodic transmitter after SlaveController setup. */
    bool start();

    /*! @brief Copy receive/transmit diagnostics for service tooling. */
    Diagnostics getDiagnostics();
}


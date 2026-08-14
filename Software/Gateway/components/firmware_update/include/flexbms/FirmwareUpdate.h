#pragma once

#include "flexbms/Protocol.h"

#include <cstddef>
#include <cstdint>

namespace FlexBms::FirmwareUpdate
{
    enum class Target : uint8_t { Gateway, Stm32 };
    enum class Phase : uint8_t { Idle, Uploading, Installing, Complete, Failed };
    enum class Stage : uint8_t { Idle, Upload, Validate, Restart, Handoff, RomBootloader, Erase, Program, Verify, Complete };

    struct Status
    {
        Phase phase = Phase::Idle;
        Target target = Target::Gateway;
        Stage stage = Stage::Idle;
        uint32_t bytesReceived = 0U;
        uint32_t bytesExpected = 0U;
        uint32_t progressBytes = 0U;
        const char *version = "";
        const char *detail = "";
    };

    // Call only for a station-LAN HTTP request after its manifest headers have
    // been checked.  The image is streamed; no complete image is kept in RAM.
    bool beginUpload(Target target, const char *version, uint32_t bytes, uint32_t crc32);
    bool writeUpload(const uint8_t *data, size_t bytes);
    bool finishUpload();
    void abortUpload(const char *detail);

    const Status &getStatus();
    bool consumeStatusChanged();
    bool isAvailable();
    bool ownsUart();
    bool consumeFramedUartReset();

    // Called from the normal UART receive path until the STM32 accepts the
    // framed bootloader handoff.  ROM bootloader bytes are then handled here.
    void onFrame(const UartV1::Frame &frame);
    void poll();
    void markGatewayBootHealthy();
}

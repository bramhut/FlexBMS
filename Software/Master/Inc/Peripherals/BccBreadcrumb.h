#pragma once

#include <cstdint>

namespace BccBreadcrumb
{
    // BKP18R is intentionally reserved for this record. It sits after the
    // energy-counter record (BKP8R..BKP17R) and before SoC calibration
    // storage (BKP28R..BKP30R).
    constexpr uint8_t kBackupRegister = 18U;

    // The record is a single packed word describing the last BCC transfer that
    // was in flight when the STM32 reset.
    void captureOnBoot();
    bool hasLastResetRecord();
    uint32_t getLastResetRecord();

    void recordTransferStart(uint8_t cid, uint8_t address, uint8_t command,
                             uint16_t rxTransferCount);
    void recordTransferComplete();
}

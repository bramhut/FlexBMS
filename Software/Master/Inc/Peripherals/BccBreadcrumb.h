#pragma once

#include <cstdint>

namespace BccBreadcrumb
{
    // BKP18R and BKP19R are intentionally reserved for reset diagnostics.
    // They sit after the energy-counter record (BKP8R..BKP17R) and before
    // SoC calibration details use BKP20R..BKP25R; BKP26R..BKP27R remain free.
    // The calibration timestamp remains in BKP28R..BKP30R.
    constexpr uint8_t kBackupRegister = 18U;
    constexpr uint8_t kWatchdogBackupRegister = 19U;

    // The record is a single packed word describing the last BCC transfer that
    // was in flight when the STM32 reset.
    void captureOnBoot();
    bool hasLastResetRecord();
    uint32_t getLastResetRecord();
    bool hasLastWatchdogRecord();
    uint32_t getLastWatchdogRecord();

    void recordTransferStart(uint8_t cid, uint8_t address, uint8_t command,
                             uint16_t rxTransferCount);
    void recordTransferComplete();

    // Persist the live watchdog classification separately from the transfer
    // breadcrumb so a post-transfer stall does not destroy the SPI context.
    void recordWatchdogState(uint8_t stalledSources, uint8_t bccPhase,
                             uint8_t pccPhase, uint8_t pccSequence,
                             uint8_t bccSequence, bool monitoringActive);
}

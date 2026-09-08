#include "BccBreadcrumb.h"

#include "main.h"

#include <algorithm>

namespace BccBreadcrumb
{
    namespace
    {
        constexpr uint32_t kMagic = 0xB0000000UL;
        constexpr uint32_t kPendingMask = 1UL << 27U;
        constexpr uint32_t kSequenceMask = 0xFFUL;
        constexpr uint32_t kCidMask = 0x3FUL;
        constexpr uint32_t kAddressMask = 0x7FUL;
        constexpr uint32_t kCommandMask = 0x03UL;
        constexpr uint32_t kRxCountMask = 0x0FUL;
        constexpr uint32_t kWatchdogMagic = 0xC0000000UL;
        constexpr uint32_t kWatchdogMonitoringMask = 1UL << 27U;

        volatile uint32_t *const record = &(TAMP->BKP0R) + kBackupRegister;
        volatile uint32_t *const watchdogRecord = &(TAMP->BKP0R) + kWatchdogBackupRegister;
        uint32_t lastResetRecord = 0U;
        uint32_t lastWatchdogRecord = 0U;
        uint8_t sequence = 0U;

        bool isValid(uint32_t value)
        {
            return (value & 0xF0000000UL) == kMagic;
        }

        bool isWatchdogValid(uint32_t value)
        {
            return (value & 0xF0000000UL) == kWatchdogMagic;
        }

        uint32_t makeRecord(uint8_t cid, uint8_t address, uint8_t command,
                            uint16_t rxTransferCount, bool pending)
        {
            const uint32_t rxCount = std::min<uint32_t>(rxTransferCount, kRxCountMask);
            return kMagic |
                   (pending ? kPendingMask : 0U) |
                   ((static_cast<uint32_t>(sequence) & kSequenceMask) << 19U) |
                   ((static_cast<uint32_t>(cid) & kCidMask) << 13U) |
                   ((static_cast<uint32_t>(address) & kAddressMask) << 6U) |
                   ((static_cast<uint32_t>(command) & kCommandMask) << 4U) |
                   (rxCount & kRxCountMask);
        }
    }

    void captureOnBoot()
    {
        const uint32_t value = *record;
        lastResetRecord = isValid(value) ? value : 0U;
        const uint32_t watchdogValue = *watchdogRecord;
        lastWatchdogRecord = isWatchdogValid(watchdogValue) ? watchdogValue : 0U;
    }

    bool hasLastResetRecord()
    {
        return lastResetRecord != 0U;
    }

    uint32_t getLastResetRecord()
    {
        return lastResetRecord;
    }

    bool hasLastWatchdogRecord()
    {
        return lastWatchdogRecord != 0U;
    }

    uint32_t getLastWatchdogRecord()
    {
        return lastWatchdogRecord;
    }

    void recordTransferStart(uint8_t cid, uint8_t address, uint8_t command,
                             uint16_t rxTransferCount)
    {
        ++sequence;
        *record = makeRecord(cid, address, command, rxTransferCount, true);
        __DMB();
    }

    void recordTransferComplete()
    {
        const uint32_t value = *record;
        if (isValid(value))
        {
            *record = value & ~kPendingMask;
            __DMB();
        }
    }

    void recordWatchdogState(uint8_t stalledSources, uint8_t bccPhase,
                             uint8_t pccPhase, uint8_t pccSequence,
                             uint8_t bccSequence, bool monitoringActive)
    {
        *watchdogRecord = kWatchdogMagic |
                          (monitoringActive ? kWatchdogMonitoringMask : 0U) |
                          ((static_cast<uint32_t>(stalledSources) & 0x03UL) << 25U) |
                          ((static_cast<uint32_t>(bccPhase) & 0x1FUL) << 20U) |
                          ((static_cast<uint32_t>(pccPhase) & 0x0FUL) << 16U) |
                          (static_cast<uint32_t>(pccSequence) << 8U) |
                          static_cast<uint32_t>(bccSequence);
        __DMB();
    }
}

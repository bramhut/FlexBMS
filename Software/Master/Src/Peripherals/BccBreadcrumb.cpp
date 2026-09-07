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

        volatile uint32_t *const record = &(TAMP->BKP0R) + kBackupRegister;
        uint32_t lastResetRecord = 0U;
        uint8_t sequence = 0U;

        bool isValid(uint32_t value)
        {
            return (value & 0xF0000000UL) == kMagic;
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
    }

    bool hasLastResetRecord()
    {
        return lastResetRecord != 0U;
    }

    uint32_t getLastResetRecord()
    {
        return lastResetRecord;
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
}

#include "Peripherals/EnergyCounter.h"

#include <cstdint>

namespace
{
    constexpr bool arithmeticCasesPass()
    {
        uint64_t remainder = 0U;
        uint64_t charged = 0U;
        charged = EnergyCounter::integrateMicroWh(charged, remainder, 300'000'000U, 640U, 1'000'000U);
        if (charged != 833'333U) return false; // 300 V * 10 A * 1 s

        uint64_t discharged = 0U;
        remainder = 0U;
        discharged = EnergyCounter::integrateMicroWh(discharged, remainder, 300'000'000U, 640U, 500'000U);
        if (discharged != 416'666U) return false;

        const uint64_t held = EnergyCounter::integrateMicroWh(discharged, remainder, 300'000'000U, 0U, 1'000'000U);
        if (held != discharged) return false;

        remainder = 0U;
        if (EnergyCounter::integrateMicroWh(UINT64_MAX - 1U, remainder, UINT32_MAX, UINT16_MAX, 1'000'000U) != UINT64_MAX)
        {
            return false;
        }
        return true;
    }

    constexpr bool backupLayoutDoesNotOverlap()
    {
        return EnergyCounter::kChargedEnergyBackupRegister == 8U &&
               EnergyCounter::kDischargedEnergyBackupRegister == 12U &&
               EnergyCounter::kChecksumBackupRegister == 16U &&
               EnergyCounter::kMarkerBackupRegister == 17U &&
               EnergyCounter::kMarkerBackupRegister < 18U;
    }

    constexpr bool backupRecordValidationPasses()
    {
        constexpr uint64_t charged = 123'456U;
        constexpr uint64_t discharged = 789'012U;
        constexpr uint32_t checksum = EnergyCounter::recordChecksum(charged, discharged);
        return EnergyCounter::recordIsValid(charged, discharged, checksum, EnergyCounter::kRecordMarker) &&
               !EnergyCounter::recordIsValid(charged + 1U, discharged, checksum, EnergyCounter::kRecordMarker) &&
               !EnergyCounter::recordIsValid(charged, discharged, checksum, 0U);
    }
}

static_assert(arithmeticCasesPass());
static_assert(backupLayoutDoesNotOverlap());
static_assert(backupRecordValidationPasses());
static_assert(EnergyCounter::elapsedMicroseconds(100U, UINT32_MAX - 99U) == 200U);

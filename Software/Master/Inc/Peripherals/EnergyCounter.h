#pragma once

#include <cstdint>

namespace EnergyCounter
{
    constexpr uint8_t kChargedEnergyBackupRegister = 8U;
    constexpr uint8_t kDischargedEnergyBackupRegister = 12U;
    constexpr uint8_t kChecksumBackupRegister = 16U;
    constexpr uint8_t kMarkerBackupRegister = 17U;
    constexpr uint32_t kRecordMarker = 0x454E4731U;
    constexpr uint32_t kChecksumSeed = 0xA3F19B27U;
    constexpr uint64_t kMicroWhDenominator = 3'600'000'000'000'000ULL;

    constexpr uint32_t elapsedMicroseconds(uint32_t now, uint32_t previous)
    {
        return now - previous;
    }

    constexpr uint32_t recordChecksum(uint64_t charged, uint64_t discharged)
    {
        uint32_t result = kChecksumSeed;
        const uint32_t words[] = {
            static_cast<uint32_t>(charged), static_cast<uint32_t>(charged >> 32U),
            static_cast<uint32_t>(discharged), static_cast<uint32_t>(discharged >> 32U),
            kRecordMarker,
        };
        for (const uint32_t word : words)
        {
            for (uint8_t byte = 0U; byte < 4U; ++byte)
            {
                result ^= static_cast<uint8_t>(word >> (byte * 8U));
                result *= 16777619U;
            }
        }
        return result;
    }

    constexpr bool recordIsValid(uint64_t charged, uint64_t discharged,
                                 uint32_t storedChecksum, uint32_t storedMarker)
    {
        return storedMarker == kRecordMarker && storedChecksum == recordChecksum(charged, discharged);
    }

    constexpr uint64_t saturatingAdd(uint64_t value, uint64_t increment)
    {
        constexpr uint64_t maximum = UINT64_MAX;
        return maximum - value < increment ? maximum : value + increment;
    }

    constexpr uint64_t integrateMicroWh(uint64_t counter, uint64_t &remainder,
                                        uint32_t voltageUv, uint32_t currentMicroAmpsMagnitude,
                                        uint32_t elapsedUs)
    {
        // Exact (uV * uA * us) / 3.6e15, without a 96-bit intermediate.
        const uint64_t product = voltageUv * static_cast<uint64_t>(currentMicroAmpsMagnitude);
        const uint64_t whole = product / kMicroWhDenominator;
        const uint64_t fraction = product % kMicroWhDenominator;
        uint64_t increment = 0, residual = 0;
        for (int bit = 31; bit >= 0; --bit) {
            increment *= 2;
            residual *= 2;
            if ((elapsedUs >> bit) & 1U) { increment += whole; residual += fraction; }
            increment += residual / kMicroWhDenominator;
            residual %= kMicroWhDenominator;
        }
        residual += remainder;
        increment += residual / kMicroWhDenominator;
        remainder = residual % kMicroWhDenominator;
        return saturatingAdd(counter, increment);
    }

    struct Snapshot
    {
        bool valid{};
        uint64_t chargedEnergyUWh{};
        uint64_t dischargedEnergyUWh{};
    };

    // Loads the single-slot RTC backup record. Must run after backup-domain
    // access has been enabled.
    void setup();

    // Integrates one complete, fresh measurement. Positive current is charge;
    // negative current is discharge. Invalid samples only end the interval.
    void update(uint32_t packVoltageUv, int32_t packCurrentMicroAmps, uint32_t timestampUs, bool valid);

    Snapshot getSnapshot();
}

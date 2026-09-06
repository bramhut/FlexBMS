#include "EnergyCounter.h"

#include "Debug.h"
#include "FreeRTOS.h"
#include "main.h"
#include "task.h"

namespace EnergyCounter
{
    namespace
    {

        volatile uint32_t *const chargedLow = &(TAMP->BKP0R) + kChargedEnergyBackupRegister;
        volatile uint32_t *const chargedHigh = chargedLow + 1U;
        volatile uint32_t *const dischargedLow = &(TAMP->BKP0R) + kDischargedEnergyBackupRegister;
        volatile uint32_t *const dischargedHigh = dischargedLow + 1U;
        volatile uint32_t *const checksumRegister = &(TAMP->BKP0R) + kChecksumBackupRegister;
        volatile uint32_t *const markerRegister = &(TAMP->BKP0R) + kMarkerBackupRegister;

        bool initialized = false;
        bool havePreviousMeasurement = false;
        uint32_t previousTimestampUs = 0U;
        uint64_t chargedRemainder = 0U;
        uint64_t dischargedRemainder = 0U;
        uint64_t chargedEnergyUWh = 0U;
        uint64_t dischargedEnergyUWh = 0U;

        uint64_t readCounter(const volatile uint32_t *low, const volatile uint32_t *high)
        {
            return static_cast<uint64_t>(*low) | (static_cast<uint64_t>(*high) << 32U);
        }

        void writeCounter(volatile uint32_t *low, volatile uint32_t *high, uint64_t value)
        {
            *low = static_cast<uint32_t>(value);
            *high = static_cast<uint32_t>(value >> 32U);
        }

        void persist()
        {
            // Invalidate first so an interruption cannot leave an old marker
            // paired with a new counter value. The marker is restored last.
            *markerRegister = 0U;
            writeCounter(chargedLow, chargedHigh, chargedEnergyUWh);
            writeCounter(dischargedLow, dischargedHigh, dischargedEnergyUWh);
            *checksumRegister = recordChecksum(chargedEnergyUWh, dischargedEnergyUWh);
            __DMB();
            *markerRegister = kRecordMarker;
            __DMB();
        }

        void integrate(uint64_t &counter, uint64_t &remainder, uint32_t voltageUv,
                       uint32_t currentRawMagnitude, uint32_t elapsedUs)
        {
            counter = integrateMicroWh(counter, remainder, voltageUv, currentRawMagnitude, elapsedUs);
        }
    }

    void setup()
    {
        taskENTER_CRITICAL();
        const uint64_t storedCharged = readCounter(chargedLow, chargedHigh);
        const uint64_t storedDischarged = readCounter(dischargedLow, dischargedHigh);
        const bool valid = recordIsValid(storedCharged, storedDischarged, *checksumRegister, *markerRegister);
        if (!valid)
        {
            PRINTF_WARN("[ENERGY] Backup record invalid; resetting counters\n");
            chargedEnergyUWh = 0U;
            dischargedEnergyUWh = 0U;
            persist();
        }
        else
        {
            chargedEnergyUWh = storedCharged;
            dischargedEnergyUWh = storedDischarged;
        }
        initialized = true;
        havePreviousMeasurement = false;
        chargedRemainder = 0U;
        dischargedRemainder = 0U;
        taskEXIT_CRITICAL();
    }

    void update(uint32_t packVoltageUv, int16_t packCurrentRaw, uint32_t timestampUs, bool valid)
    {
        if (!initialized)
        {
            return;
        }

        taskENTER_CRITICAL();
        if (!valid || packVoltageUv == 0U)
        {
            havePreviousMeasurement = false;
            taskEXIT_CRITICAL();
            return;
        }

        if (!havePreviousMeasurement)
        {
            previousTimestampUs = timestampUs;
            havePreviousMeasurement = true;
            taskEXIT_CRITICAL();
            return;
        }

        const uint32_t elapsedUs = elapsedMicroseconds(timestampUs, previousTimestampUs);
        previousTimestampUs = timestampUs;
        if (elapsedUs == 0U)
        {
            taskEXIT_CRITICAL();
            return;
        }

        const uint32_t currentRawMagnitude = packCurrentRaw < 0
                                                 ? static_cast<uint32_t>(-(static_cast<int32_t>(packCurrentRaw)))
                                                 : static_cast<uint32_t>(packCurrentRaw);
        if (packCurrentRaw > 0)
        {
            integrate(chargedEnergyUWh, chargedRemainder, packVoltageUv, currentRawMagnitude, elapsedUs);
        }
        else
        {
            integrate(dischargedEnergyUWh, dischargedRemainder, packVoltageUv, currentRawMagnitude, elapsedUs);
        }
        persist();
        taskEXIT_CRITICAL();
    }

    Snapshot getSnapshot()
    {
        Snapshot snapshot{};
        taskENTER_CRITICAL();
        snapshot.valid = initialized;
        snapshot.chargedEnergyUWh = chargedEnergyUWh;
        snapshot.dischargedEnergyUWh = dischargedEnergyUWh;
        taskEXIT_CRITICAL();
        return snapshot;
    }
}

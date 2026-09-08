#include "Peripherals/bcc/MeasurementFrame.h"

#include <cstdint>
#include <type_traits>

namespace
{
    constexpr SlaveController::MeasurementFrame makeFrame(uint8_t monitorCount, uint32_t sequence)
    {
        SlaveController::MeasurementFrame frame{};
        frame.summary.valid = true;
        frame.summary.measurementsFresh = true;
        frame.summary.socValid = true;
        frame.summary.currentSensingEnabled = true;
        frame.summary.sequence = sequence;
        frame.summary.packVoltageUv = 48'000'000U + sequence;
        frame.summary.packCurrentA = 12.5;
        frame.summary.socRaw = 42U;
        frame.summary.minCellVoltageUv = 3'100'000U;
        frame.summary.maxCellVoltageUv = 3'500'000U;
        frame.summary.minNtcTemperatureRaw = 100U;
        frame.summary.maxNtcTemperatureRaw = 200U;
        frame.summary.minIcTemperatureRaw = 300U;
        frame.summary.maxIcTemperatureRaw = 400U;
        frame.monitorCount = monitorCount;

        for (uint8_t monitorIndex = 0U; monitorIndex < monitorCount; ++monitorIndex)
        {
            auto &monitor = frame.monitors[monitorIndex];
            monitor.cellCount = (monitorIndex & 1U) == 0U ? 14U : 6U;
            monitor.ntcCount = (monitorIndex & 1U) == 0U ? 7U : 0U;
            monitor.icTemperatureRaw = static_cast<uint16_t>(500U + monitorIndex);
            monitor.balancingMask = static_cast<uint16_t>(1U << (monitorIndex % 12U));
            for (uint8_t cellIndex = 0U; cellIndex < monitor.cellCount; ++cellIndex)
            {
                monitor.cellVoltagesUv[cellIndex] =
                    3'000'000U + static_cast<uint32_t>(monitorIndex) * 100U + cellIndex;
            }
            for (uint8_t ntcIndex = 0U; ntcIndex < monitor.ntcCount; ++ntcIndex)
            {
                monitor.ntcTemperaturesRaw[ntcIndex] =
                    static_cast<uint16_t>(600U + monitorIndex * 10U + ntcIndex);
            }
        }
        return frame;
    }

    constexpr bool activeCopiesPass()
    {
        for (const uint8_t count : {1U, 8U, 32U})
        {
            const auto source = makeFrame(count, count);
            SlaveController::MeasurementFrame destination{};
            destination.monitors[31].icTemperatureRaw = 0xBEEFU;
            if (!SlaveController::copyActiveMeasurementFrame(source, destination) ||
                destination.monitorCount != count ||
                destination.summary.sequence != count ||
                destination.summary.packVoltageUv != source.summary.packVoltageUv ||
                destination.monitors[count - 1U].cellCount != source.monitors[count - 1U].cellCount ||
                destination.monitors[count - 1U].ntcCount != source.monitors[count - 1U].ntcCount ||
                destination.monitors[count - 1U].balancingMask != source.monitors[count - 1U].balancingMask ||
                destination.monitors[0].cellVoltagesUv[13] != source.monitors[0].cellVoltagesUv[13] ||
                destination.monitors[0].ntcTemperaturesRaw[6] != source.monitors[0].ntcTemperaturesRaw[6])
            {
                return false;
            }
            if (count < 32U && destination.monitors[31].icTemperatureRaw != 0xBEEFU)
            {
                return false;
            }
        }
        return true;
    }

    constexpr bool sequenceRulesPass()
    {
        const auto source = makeFrame(8U, 17U);
        SlaveController::MeasurementFrame destination{};
        destination.summary.sequence = 99U;
        uint32_t lastSeen = 17U;

        if (SlaveController::copyNewMeasurementFrame(source, 17U, lastSeen, destination) ||
            lastSeen != 17U || destination.summary.sequence != 99U)
        {
            return false;
        }
        if (SlaveController::copyNewMeasurementFrame(source, 18U, lastSeen, destination) ||
            lastSeen != 17U || destination.summary.sequence != 99U)
        {
            return false;
        }

        lastSeen = 16U;
        return SlaveController::copyNewMeasurementFrame(source, 17U, lastSeen, destination) &&
               lastSeen == 17U && destination.summary.sequence == 17U &&
               destination.monitorCount == 8U;
    }

    constexpr bool invalidCapacityLeavesDestinationUntouched()
    {
        auto source = makeFrame(1U, 4U);
        source.monitorCount = 33U;
        SlaveController::MeasurementFrame destination{};
        destination.summary.sequence = 55U;
        return !SlaveController::copyActiveMeasurementFrame(source, destination) &&
               destination.summary.sequence == 55U;
    }
}

static_assert(SlaveController::MAX_MEASUREMENT_SLAVES == 32U);
static_assert(SlaveController::MAX_MEASUREMENT_CELLS == 14U);
static_assert(SlaveController::MAX_MEASUREMENT_NTCS == 7U);
static_assert(std::is_trivially_copyable_v<SlaveController::MeasurementFrame>);
static_assert(sizeof(SlaveController::MeasurementFrame) <= 3U * 1024U);
static_assert(activeCopiesPass());
static_assert(sequenceRulesPass());
static_assert(invalidCapacityLeavesDestinationUntouched());

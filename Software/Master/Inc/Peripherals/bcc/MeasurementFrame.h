#pragma once

#include "RuntimeConfiguration.h"
#include "bcc/bcc_utils.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace SlaveController
{
    constexpr std::size_t MAX_MEASUREMENT_SLAVES = RuntimeConfiguration::MAX_SLAVES;
    constexpr std::size_t MAX_MEASUREMENT_CELLS = BCC_MAX_CELLS;
    constexpr std::size_t MAX_MEASUREMENT_NTCS = BCC_GPIO_INPUT_CNT;

    static_assert(MAX_MEASUREMENT_SLAVES == 32U);
    static_assert(MAX_MEASUREMENT_CELLS == 14U);
    static_assert(MAX_MEASUREMENT_NTCS == 7U);

    struct MeasurementSummary
    {
        bool valid{};
        bool measurementsFresh{};
        bool socValid{};
        bool currentSensingEnabled{};
        uint32_t sequence{};
        uint32_t packVoltageUv{};
        double packCurrentA{};
        uint16_t socRaw{};
        uint32_t minCellVoltageUv{};
        uint32_t maxCellVoltageUv{};
        uint16_t minNtcTemperatureRaw{};
        uint16_t maxNtcTemperatureRaw{};
        uint16_t minIcTemperatureRaw{};
        uint16_t maxIcTemperatureRaw{};
    };

    struct MonitorMeasurements
    {
        uint8_t cellCount{};
        uint8_t ntcCount{};
        std::array<uint32_t, MAX_MEASUREMENT_CELLS> cellVoltagesUv{};
        std::array<uint16_t, MAX_MEASUREMENT_NTCS> ntcTemperaturesRaw{};
        uint16_t icTemperatureRaw{};
        // Cells selected for the current balancing pulse. Brief measurement
        // pauses do not clear this mask; it is not instantaneous CB_DRV_STS.
        uint16_t balancingMask{};
    };

    struct MeasurementFrame
    {
        MeasurementSummary summary{};
        uint8_t monitorCount{};
        std::array<MonitorMeasurements, MAX_MEASUREMENT_SLAVES> monitors{};
    };

    [[nodiscard]] constexpr bool copyActiveMeasurementFrame(
        const MeasurementFrame &source,
        MeasurementFrame &destination)
    {
        if (source.monitorCount > MAX_MEASUREMENT_SLAVES)
        {
            return false;
        }

        destination.summary = source.summary;
        destination.monitorCount = source.monitorCount;
        for (std::size_t index = 0U; index < source.monitorCount; ++index)
        {
            destination.monitors[index] = source.monitors[index];
        }
        return true;
    }

    [[nodiscard]] constexpr bool copyNewMeasurementFrame(
        const MeasurementFrame &source,
        uint32_t publishedSequence,
        uint32_t &lastSeenSequence,
        MeasurementFrame &destination)
    {
        if (publishedSequence == lastSeenSequence ||
            source.summary.sequence != publishedSequence ||
            !copyActiveMeasurementFrame(source, destination))
        {
            return false;
        }

        lastSeenSequence = publishedSequence;
        return true;
    }

    static_assert(std::is_trivially_copyable_v<MeasurementFrame>);
    static_assert(sizeof(MeasurementFrame) <= 3U * 1024U);
}

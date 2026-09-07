#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace BatteryBalancing
{
    struct Selection
    {
        uint16_t mask{};
        uint8_t nextStartIndex{};
    };

    constexpr bool currentAllowsBalancing(double packCurrentA,
                                          double capacityAh,
                                          double minimumCurrentA,
                                          double maximumCurrentC)
    {
        return capacityAh > 0.0 &&
               packCurrentA >= minimumCurrentA &&
               packCurrentA <= capacityAh * maximumCurrentC;
    }

    constexpr Selection selectCells(std::span<const uint32_t> cellVoltagesUv,
                                    uint32_t packMinimumCellVoltageUv,
                                    uint32_t minimumBalancingVoltageUv,
                                    uint32_t startDifferenceUv,
                                    uint32_t stopDifferenceUv,
                                    uint16_t previousMask,
                                    uint8_t startIndex,
                                    uint8_t maximumSelectedCells)
    {
        Selection result{};
        if (cellVoltagesUv.empty() || maximumSelectedCells == 0U)
        {
            return result;
        }

        const size_t cellCount = std::min<size_t>(cellVoltagesUv.size(), 16U);
        const size_t first = startIndex % cellCount;
        size_t nextStart = (first + 1U) % cellCount;
        uint8_t selectedCount = 0U;

        for (size_t offset = 0U; offset < cellCount; ++offset)
        {
            const size_t cellIndex = (first + offset) % cellCount;
            const uint32_t voltageUv = cellVoltagesUv[cellIndex];
            const uint32_t differenceUv = voltageUv >= packMinimumCellVoltageUv
                                              ? voltageUv - packMinimumCellVoltageUv
                                              : 0U;
            const bool wasSelected = (previousMask & (1U << cellIndex)) != 0U;
            const uint32_t requiredDifferenceUv = wasSelected
                                                      ? stopDifferenceUv
                                                      : startDifferenceUv;
            const bool differenceAllowsBalancing = wasSelected
                                                        ? differenceUv > requiredDifferenceUv
                                                        : differenceUv >= requiredDifferenceUv;

            if (voltageUv >= minimumBalancingVoltageUv &&
                differenceAllowsBalancing)
            {
                result.mask |= static_cast<uint16_t>(1U << cellIndex);
                if (++selectedCount >= maximumSelectedCells)
                {
                    nextStart = (cellIndex + 1U) % cellCount;
                    break;
                }
            }
        }

        // Rotate the priority by one cell each cycle so a thermal channel cap
        // cannot permanently starve a later cell.
        result.nextStartIndex = static_cast<uint8_t>(nextStart);
        return result;
    }
}

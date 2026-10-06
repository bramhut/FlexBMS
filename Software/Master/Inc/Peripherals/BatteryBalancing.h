#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace BatteryBalancing
{
    inline constexpr uint32_t QualificationMs = 30'000U;
    inline constexpr uint32_t ShortPulseMs = 30'000U;
    // Qualification gates new pulses only; safety checks remain independent.
    class CurrentQualification
    {
    public:
        constexpr void reset() { tracking = false; }
        constexpr bool update(bool inRange, uint32_t now)
        {
            if (!inRange) { reset(); return false; }
            if (!tracking) { since = now; tracking = true; }
            return static_cast<uint32_t>(now - since) >= QualificationMs;
        }
    private:
        uint32_t since = 0U;
        bool tracking = false;
    };

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

        while (selectedCount < maximumSelectedCells)
        {
            size_t highestCellIndex = cellCount;
            for (size_t offset = 0U; offset < cellCount; ++offset)
            {
                const size_t cellIndex = (first + offset) % cellCount;
                const uint16_t cellBit = static_cast<uint16_t>(1U << cellIndex);
                if ((result.mask & cellBit) != 0U)
                {
                    continue;
                }

                const uint32_t voltageUv = cellVoltagesUv[cellIndex];
                const uint32_t differenceUv = voltageUv >= packMinimumCellVoltageUv
                                                  ? voltageUv - packMinimumCellVoltageUv
                                                  : 0U;
                const bool wasSelected = (previousMask & cellBit) != 0U;
                const uint32_t requiredDifferenceUv = wasSelected
                                                          ? stopDifferenceUv
                                                          : startDifferenceUv;
                const bool differenceAllowsBalancing = wasSelected
                                                            ? differenceUv > requiredDifferenceUv
                                                            : differenceUv >= requiredDifferenceUv;
                if (voltageUv < minimumBalancingVoltageUv || !differenceAllowsBalancing)
                {
                    continue;
                }

                // Voltage is the primary priority. Iterating from the rotating
                // start index makes that rotation only a tie-breaker for cells
                // with equal measured voltage.
                if (highestCellIndex == cellCount ||
                    voltageUv > cellVoltagesUv[highestCellIndex])
                {
                    highestCellIndex = cellIndex;
                }
            }

            if (highestCellIndex == cellCount)
            {
                break;
            }

            result.mask |= static_cast<uint16_t>(1U << highestCellIndex);
            ++selectedCount;
            nextStart = (highestCellIndex + 1U) % cellCount;
        }

        // Rotation affects only equal-voltage ties; a lower-voltage cell can
        // never displace a higher eligible cell because of its index.
        result.nextStartIndex = static_cast<uint8_t>(nextStart);
        return result;
    }
}

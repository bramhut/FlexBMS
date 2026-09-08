#include "Peripherals/BatteryBalancing.h"

#include <array>

namespace
{
    constexpr bool selectionUsesThresholdsAndHysteresis()
    {
        constexpr std::array<uint32_t, 4U> cells{
            3'450'000U, 3'459'000U, 3'460'000U, 3'470'000U};
        const auto initial = BatteryBalancing::selectCells(
            cells, 3'450'000U, 3'400'000U, 10'000U, 5'000U, 0U, 0U, 4U);
        const auto continued = BatteryBalancing::selectCells(
            cells, 3'450'000U, 3'400'000U, 10'000U, 5'000U,
            static_cast<uint16_t>(1U << 1U), 0U, 4U);
        constexpr std::array<uint32_t, 2U> stopBoundary{3'450'000U, 3'455'000U};
        const auto stopped = BatteryBalancing::selectCells(
            stopBoundary, 3'450'000U, 3'400'000U, 10'000U, 5'000U,
            static_cast<uint16_t>(1U << 1U), 0U, 2U);

        return initial.mask == 0b1100U &&
               continued.mask == 0b1110U &&
               stopped.mask == 0U;
    }

    constexpr bool selectionPrioritizesHighestVoltages()
    {
        constexpr std::array<uint32_t, 8U> cells{
            3'431'000U, 3'480'000U, 3'442'000U, 3'475'000U,
            3'497'000U, 3'432'000U, 3'467'000U, 3'470'000U};
        const auto selection = BatteryBalancing::selectCells(
            cells, 3'413'000U, 3'400'000U, 10'000U, 5'000U, 0U, 6U, 3U);

        return selection.mask == 0b00011010U;
    }

    constexpr bool equalVoltageSelectionIsCappedAndRotates()
    {
        constexpr std::array<uint32_t, 8U> cells{
            3'470'000U, 3'470'000U, 3'470'000U, 3'470'000U,
            3'470'000U, 3'470'000U, 3'470'000U, 3'470'000U};
        const auto first = BatteryBalancing::selectCells(
            cells, 3'450'000U, 3'400'000U, 10'000U, 5'000U, 0U, 0U, 3U);
        const auto second = BatteryBalancing::selectCells(
            cells, 3'450'000U, 3'400'000U, 10'000U, 5'000U,
            first.mask, first.nextStartIndex, 3U);

        return first.mask == 0b00000111U &&
               first.nextStartIndex == 3U &&
               second.mask == 0b00111000U &&
               second.nextStartIndex == 6U;
    }

    constexpr bool currentGateIsDirectional()
    {
        return BatteryBalancing::currentAllowsBalancing(31.4, 314.0, -0.1, 0.1) &&
               !BatteryBalancing::currentAllowsBalancing(31.5, 314.0, -0.1, 0.1) &&
               !BatteryBalancing::currentAllowsBalancing(-0.2, 314.0, -0.1, 0.1);
    }
}

static_assert(selectionUsesThresholdsAndHysteresis());
static_assert(selectionPrioritizesHighestVoltages());
static_assert(equalVoltageSelectionIsCappedAndRotates());
static_assert(currentGateIsDirectional());

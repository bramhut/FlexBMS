#pragma once
#include <cstdint>

namespace CurrentSensingPolicy
{
    constexpr bool isPackCurrentSource(uint8_t selectedSlave, uint8_t slave)
    {
        return selectedSlave != 0U && selectedSlave == slave;
    }

    constexpr bool measurementCircuitEnabled(bool configured, bool selected, bool equalize)
    {
        return configured && (equalize || selected);
    }
}

#pragma once

#include <cstdint>

namespace RuntimeConfiguration
{
    constexpr uint16_t LEGACY_CONFIG_VERSION = 2U;
    constexpr uint16_t CONFIG_VERSION = 3U;
    constexpr uint8_t MAX_SLAVES = 32U;
    constexpr uint8_t CURRENT_SENSE_NONE = 0U;
    constexpr uint32_t APPLICATION_FLASH_BYTES = 508U * 1024U;

    struct Values
    {
        uint8_t slaveCount{};
        uint8_t currentSenseSlave{};
        uint32_t shuntResistanceMicroOhms{};
        uint32_t batteryCapacityMilliAh{};
        bool invertCurrent{};
        bool balanceEnabled{};
        bool startupDiagnostics{};
    };

    enum class LoadStatus : uint8_t
    {
        Valid,
        Blank,
        VersionMismatch,
        Corrupt,
    };

    struct LoadResult
    {
        LoadStatus status{LoadStatus::Blank};
        uint16_t storedVersion{};
        Values values{};
    };

    const Values &defaults();
    LoadResult load();
    bool validate(const Values &values);
    bool save(const Values &values);
    bool updateBalanceEnabled(bool enabled);
}

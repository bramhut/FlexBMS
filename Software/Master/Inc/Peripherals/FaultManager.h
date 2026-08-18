#pragma once

#include <cstdint>

namespace FaultManager
{
    // The manager owns these identities, their severity, latching and status
    // aggregation. Producers may only update the active condition assigned to
    // them; they cannot clear latches or command HV outputs.
    enum class BmsFault : uint8_t
    {
        ConfigurationInvalid = 0U,
        SlaveUnavailable,
        BccDiagnostics,
        CellVoltageLimit,
        ThermalLimit,
        CurrentLimit,
        BccIntegrity,
        AdcFault,
        BalancingHardwareFault,
        BccCommunication,
    };

    enum class HvFault : uint8_t
    {
        SensorDiagnostic = 0U,
        BatteryVoltageMismatch,
        LoadSideEnergized,
        PrechargeTimeout,
        PrechargeVoltageLost,
        ContactorVoltageLost,
    };

    enum class Warning : uint8_t
    {
        WatchdogReset = 0U,
        StartupDiagnosticsBypassed,
        BatteryVoltageMismatchOff,
    };

    enum class BmsState : uint8_t
    {
        Starting = 0U,
        Ready = 1U,
        Running = 2U,
        Error = 3U,
        Critical = 4U,
    };

    struct Snapshot
    {
        uint32_t bmsActive{};
        uint32_t bmsLatched{};
        uint32_t hvActive{};
        uint32_t hvLatched{};
        uint32_t warnings{};
        BmsState bmsState{BmsState::Starting};
    };

    void setup();

    void setBmsFault(BmsFault fault, bool active);
    void setHvFault(HvFault fault, bool active);
    void setWarning(Warning warning, bool active);

    // For unrecoverable current-software failures. No critical bitmap exists.
    void enterCritical();

    // Startup completion and HV RUN state are status inputs; state aggregation
    // itself remains owned by this manager.
    void setStartupComplete(bool complete);
    void setHvRunning(bool running);

    // Acknowledges every eligible latch without changing the run request. It
    // is denied while any ERROR condition is active or state is CRITICAL.
    bool acknowledge();

    Snapshot getSnapshot();
    bool canEnableHv();
    bool hasBlockingErrors();
}

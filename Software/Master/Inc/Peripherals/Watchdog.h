#pragma once

#include <cstdint>

namespace Watchdog
{
    enum class BccPhase : uint8_t
    {
        Uninitialized = 0U,
        LoopStart,
        DeviceInitialization,
        RegisterInitialization,
        Diagnostics,
        MeasurementStart,
        MeasurementRead,
        RegisterRequests,
        FaultDetection,
        Balancing,
        PresenceCheck,
        SnapshotPublication,
        EnergyUpdate,
        Delay,
        Critical,
    };

    enum class PccPhase : uint8_t
    {
        Uninitialized = 0U,
        Idle,
        WaitingForStateLock,
        Running,
    };

    // The independent watchdog is refreshed only while both safety workers
    // continue to make progress. A stalled worker therefore resets the MCU.
    void setup();
    void reportPccProgress();
    void reportBccProgress();
    void setPccPhase(PccPhase phase);
    void setBccPhase(BccPhase phase);
    void loop();
}

#pragma once

#include <cstdint>

namespace FlexBms::TimeSync
{
    enum class State : uint8_t
    {
        WaitingForNetwork,
        WaitingForNtp,
        WaitingForStm32,
        Synchronized,
    };

    struct Status
    {
        State state = State::WaitingForNetwork;
        bool hasLastSync = false;
        uint32_t lastSyncUnixS = 0U;
    };

    // Enables time synchronisation.  The ESP-IDF SNTP client is created and
    // destroyed by setStationConnected(), so provisioning never emits NTP.
    bool start();
    void setStationConnected(bool connected);

    // A completed NTP sample remains pending until the STM32 acknowledges its
    // SET_RTC request.  Failed delivery is retried after one minute.
    bool pendingStm32Sync(uint32_t &unixTime);
    void stm32SyncStarted(uint32_t unixTime);
    void completeStm32Sync(bool success);

    Status getStatus();
    bool consumeStatusChanged();
    const char *stateName(State state);
}

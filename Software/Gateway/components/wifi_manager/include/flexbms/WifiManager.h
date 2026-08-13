#pragma once

#include <cstdint>

namespace FlexBms::Wifi
{
    enum class State : uint8_t
    {
        Unavailable,
        Provisioning,
        Connecting,
        Connected,
    };

    // Starts either first-time provisioning AP mode or the saved station connection.
    // It never blocks the BMS UART path. false means Wi-Fi is unavailable for this boot.
    bool start();

    // This is intentionally only Wi-Fi state. MQTT ownership is added later.
    State getState();
}

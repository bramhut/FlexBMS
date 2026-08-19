#pragma once

#include "flexbms/Protocol.h"

#include <cstdint>

namespace FlexBms::Mqtt
{
    enum class State : uint8_t { Unavailable, Connecting, Connected, Lost };
    using RunRequestSender = bool (*)(bool requested);

    struct Settings
    {
        bool configured = false;
        const char *host = "";
        uint16_t port = 1883U;
        const char *username = "";
    };

    // The client is local-LAN only. It is deliberately not started while the
    // Gateway is operating its open provisioning/recovery AP.
    bool start(RunRequestSender runRequestSender);
    void tick(bool stationConnected);
    State getState();
    const char *stateName(State state);
    bool consumeStatusChanged();
    Settings getSettings();

    // Credentials are validated, persisted write-only in NVS, and never
    // returned. A configuration change reconnects the client on the next tick.
    bool configure(const char *host, uint16_t port, const char *username, const char *password);

    // Accept only canonical, CRC-valid UART telemetry from the normal Gateway
    // receive path. MQTT never sends BMS wire frames.
    void publishFrame(const UartV1::Frame &frame, uint32_t gatewayUptimeMs);
    void setUartHealthy(bool healthy);
}

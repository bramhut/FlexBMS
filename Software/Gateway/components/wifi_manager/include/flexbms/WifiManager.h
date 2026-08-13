#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace FlexBms::Wifi
{
    enum class State : uint8_t
    {
        Unavailable,
        Provisioning,
        Connecting,
        Connected,
        Recovery,
    };

    struct AccessPoint
    {
        bool active = false;
        std::array<char, 33U> ssid{};
        std::array<char, 16U> address{};
    };

    struct ScanNetwork
    {
        std::array<char, 33U> ssid{};
        int8_t rssi = 0;
        bool secure = false;
    };

    struct ScanResults
    {
        std::array<ScanNetwork, 20U> networks{};
        size_t count = 0U;
    };

    enum class ScanRequestResult : uint8_t { Started, Busy, RateLimited, Unavailable };

    // Starts the ESP32 network stack without blocking the BMS UART path. The
    // Gateway owns the HTTP server in every Wi-Fi state.
    bool start();
    void tick();
    State getState();
    bool consumeStatusChanged();

    // Station credentials are write-only. The SSID is exposed only as local
    // status; the password is never returned from this component.
    const char *getStationSsid();
    AccessPoint getAccessPoint();
    bool isAccessPointActive();
    bool allowsBmsServices();
    bool configure(const char *ssid, const char *password);

    ScanRequestResult requestScan();
    bool consumeScanResults(ScanResults &results);
}

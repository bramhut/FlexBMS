#pragma once

#include <cstdint>

namespace FlexBms
{
    class StatusLed
    {
    public:
        // The IO1/GPIO17 LED is active-low. Tune this independently during
        // commissioning; it remains below the hardware maximum by default.
        static constexpr uint8_t kYellowBrightnessPercent = 35U;

        void setup();
        void update();

        void setWifiWaiting(bool active);
        void setMqttUnavailable(bool active);
        void setUartLinkLost(bool active);
        void setFirmwareUpdateActive(bool active);
        void setFatalLocalFailure(bool active);

    private:
        bool wifiWaiting = true;
        bool mqttUnavailable = false;
        bool uartLinkLost = false;
        bool firmwareUpdateActive = false;
        bool fatalLocalFailure = false;
        int64_t bootStartedUs = 0;
    };
}

#pragma once

#include <cstdint>

namespace FlexBms
{
    class StatusLed
    {
    public:
        // The board's USR_LED is active-high on GPIO1 / WROOM-02U IO1 (pin 17).
        // GPIO17 is the module flash SPIQ signal and must never be configured.
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

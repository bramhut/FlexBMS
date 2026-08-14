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

        // Follow the STM32's shared 2-second LED phase while the UART is
        // healthy.  STATUS frames refresh this estimate at least every 500 ms.
        void synchronizeToStm32Uptime(uint32_t uptimeMs);

    private:
        bool wifiWaiting = true;
        bool mqttUnavailable = false;
        bool uartLinkLost = false;
        bool firmwareUpdateActive = false;
        bool fatalLocalFailure = false;
        int64_t bootStartedUs = 0;
        bool stm32PhaseAvailable = false;
        int64_t stm32PhaseAtReceiptUs = 0;
        int64_t stm32PhaseReceivedUs = 0;
    };
}

#include "flexbms/StatusLed.h"

#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_timer.h"

#include <cstdlib>

namespace FlexBms
{
    namespace
    {
        constexpr gpio_num_t kStatusLedGpio = GPIO_NUM_17;
        constexpr ledc_mode_t kLedcMode = LEDC_LOW_SPEED_MODE;
        constexpr ledc_timer_t kLedcTimer = LEDC_TIMER_0;
        constexpr ledc_channel_t kLedcChannel = LEDC_CHANNEL_0;
        constexpr uint32_t kPwmFrequencyHz = 5000U;
        constexpr uint32_t kPwmMaximumDuty = 255U;
        constexpr int64_t kBootAcknowledgementUs = 1'000'000;
        constexpr int64_t kWaitingHalfPeriodUs = 500'000;
        constexpr int64_t kHeartbeatPeriodUs = 2'000'000;
        constexpr int64_t kHeartbeatOnUs = 100'000;
        constexpr int64_t kCodeFlashUs = 150'000;
        constexpr int64_t kCodeGapUs = 150'000;
        constexpr int64_t kUpdateHalfPeriodUs = 125'000;

        void check(esp_err_t result)
        {
            if (result != ESP_OK)
            {
                abort();
            }
        }

        bool periodicOn(int64_t now, int64_t period, int64_t onTime)
        {
            return (now % period) < onTime;
        }

        bool codeOn(int64_t now, uint8_t flashes)
        {
            constexpr int64_t kCodePeriodUs = 2'000'000;
            const int64_t phase = now % kCodePeriodUs;
            for (uint8_t flash = 0U; flash < flashes; ++flash)
            {
                const int64_t start = static_cast<int64_t>(flash) * (kCodeFlashUs + kCodeGapUs);
                if (phase >= start && phase < start + kCodeFlashUs)
                {
                    return true;
                }
            }
            return false;
        }
    }

    void StatusLed::setup()
    {
        const ledc_timer_config_t timerConfig = {
            .speed_mode = kLedcMode,
            .duty_resolution = LEDC_TIMER_8_BIT,
            .timer_num = kLedcTimer,
            .freq_hz = kPwmFrequencyHz,
            .clk_cfg = LEDC_AUTO_CLK,
            .deconfigure = false,
        };
        const ledc_channel_config_t channelConfig = {
            .gpio_num = kStatusLedGpio,
            .speed_mode = kLedcMode,
            .channel = kLedcChannel,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = kLedcTimer,
            .duty = kPwmMaximumDuty,
            .hpoint = 0,
            .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
            .flags = {},
        };
        check(ledc_timer_config(&timerConfig));
        check(ledc_channel_config(&channelConfig));
        bootStartedUs = esp_timer_get_time();
    }

    void StatusLed::update()
    {
        const int64_t now = esp_timer_get_time();
        bool on = false;
        if (now - bootStartedUs < kBootAcknowledgementUs)
        {
            on = true;
        }
        else if (fatalLocalFailure)
        {
            on = true;
        }
        else if (firmwareUpdateActive)
        {
            on = periodicOn(now, kUpdateHalfPeriodUs * 2, kUpdateHalfPeriodUs);
        }
        else if (uartLinkLost)
        {
            on = codeOn(now, 3U);
        }
        else if (wifiWaiting)
        {
            on = periodicOn(now, kWaitingHalfPeriodUs * 2, kWaitingHalfPeriodUs);
        }
        else if (mqttUnavailable)
        {
            on = codeOn(now, 2U);
        }
        else
        {
            on = periodicOn(now, kHeartbeatPeriodUs, kHeartbeatOnUs);
        }

        // Active-low hardware: logical on is a capped low-duty PWM level;
        // logical off is a continuously high output.
        const uint32_t activeLowDuty = kPwmMaximumDuty -
            (kPwmMaximumDuty * kYellowBrightnessPercent / 100U);
        check(ledc_set_duty(kLedcMode, kLedcChannel, on ? activeLowDuty : kPwmMaximumDuty));
        check(ledc_update_duty(kLedcMode, kLedcChannel));
    }

    void StatusLed::setWifiWaiting(bool active) { wifiWaiting = active; }
    void StatusLed::setMqttUnavailable(bool active) { mqttUnavailable = active; }
    void StatusLed::setUartLinkLost(bool active) { uartLinkLost = active; }
    void StatusLed::setFirmwareUpdateActive(bool active) { firmwareUpdateActive = active; }
    void StatusLed::setFatalLocalFailure(bool active) { fatalLocalFailure = active; }
}

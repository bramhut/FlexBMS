#include "flexbms/Protocol.h"
#include "flexbms/StatusLed.h"
#include "flexbms/WifiManager.h"

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#include <array>
#include <cinttypes>

namespace
{
    constexpr const char *kLogTag = "flexbms_gateway";
    constexpr uart_port_t kBmsUart = UART_NUM_1;
    constexpr int kBmsTxGpio = GPIO_NUM_2;
    constexpr int kBmsRxGpio = GPIO_NUM_3;
    constexpr int kBaudRate = 1'000'000;
    constexpr int64_t kHeartbeatPeriodUs = 500'000;
    constexpr int64_t kGatewayLossUs = 1'500'000;

    int64_t lastValidFrameUs = 0;
    bool linkWasHealthy = false;
    bool uartLinkTimedOut = false;

    void check(esp_err_t result)
    {
        if (result != ESP_OK)
        {
            ESP_LOGE(kLogTag, "fatal UART setup error: %s", esp_err_to_name(result));
            abort();
        }
    }

    void sendHeartbeat()
    {
        FlexBms::UartV1::Frame heartbeat{.type = FlexBms::UartV1::MessageType::Heartbeat, .sequence = 0U, .length = 0U};
        std::array<uint8_t, FlexBms::UartV1::kMaxFrameBytes> bytes{};
        const size_t length = FlexBms::UartV1::encode(heartbeat, bytes.data(), bytes.size());
        if (length == 0U || uart_write_bytes(kBmsUart, bytes.data(), length) != static_cast<int>(length))
        {
            ESP_LOGW(kLogTag, "UART heartbeat transmit failed");
        }
    }

    void logTelemetry(const FlexBms::UartV1::Frame &frame)
    {
        using namespace FlexBms::UartV1;
        switch (frame.type)
        {
        case MessageType::Status:
        {
            Status status{};
            if (decodeStatus(frame, status))
            {
                ESP_LOGI(kLogTag, "STATUS bms=%u hv=%u slaves=%u flags=0x%04X active=0x%04X",
                         status.bmsState, status.hvState, status.slaveCount, status.flags, status.bmsActiveFaults);
            }
            break;
        }
        case MessageType::Pack:
        {
            Pack pack{};
            if (decodePack(frame, pack))
            {
                ESP_LOGI(kLogTag, "PACK voltage=%" PRIu32 "uV current_raw=%d soc_raw=%u",
                         pack.packVoltageUv, pack.packCurrentRaw, pack.socRaw);
            }
            break;
        }
        case MessageType::Cell:
        {
            Cell cell{};
            if (decodeCell(frame, cell))
            {
                ESP_LOGI(kLogTag, "CELL slave=%u cell0=%" PRIu32 "uV balance=0x%04X",
                         cell.slaveIndex, cell.voltageUv[0], cell.balanceMask);
            }
            break;
        }
        case MessageType::Temperature:
        {
            Temperature temperature{};
            if (decodeTemperature(frame, temperature))
            {
                ESP_LOGI(kLogTag, "TEMPERATURE slave=%u ntc0_raw=%u ic_raw=%u",
                         temperature.slaveIndex, temperature.ntcRaw[0], temperature.icRaw);
            }
            break;
        }
        case MessageType::Heartbeat:
            ESP_LOGD(kLogTag, "STM32 heartbeat");
            break;
        default:
            ESP_LOGD(kLogTag, "frame type=0x%02X sequence=%u length=%u",
                     static_cast<unsigned>(frame.type), frame.sequence, frame.length);
            break;
        }
    }

    void configureUart()
    {
        uart_config_t config{};
        config.baud_rate = kBaudRate;
        config.data_bits = UART_DATA_8_BITS;
        config.parity = UART_PARITY_DISABLE;
        config.stop_bits = UART_STOP_BITS_1;
        config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
        config.rx_flow_ctrl_thresh = 0;
        config.source_clk = UART_SCLK_DEFAULT;
        check(uart_param_config(kBmsUart, &config));
        check(uart_set_pin(kBmsUart, kBmsTxGpio, kBmsRxGpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
        check(uart_driver_install(kBmsUart, 2048, 2048, 0, nullptr, 0));
    }
}

extern "C" void app_main(void)
{
    if (!FlexBms::UartV1::verifyCodec())
    {
        ESP_LOGE(kLogTag, "UART v1 codec self-test failed");
        abort();
    }

    configureUart();
    FlexBms::StatusLed statusLed;
    statusLed.setup();
    ESP_LOGI(kLogTag, "UART v1 ready: UART1, GPIO2 TX -> STM32 PA10, GPIO3 RX <- STM32 PA9");
    if (!FlexBms::Wifi::start())
    {
        ESP_LOGW(kLogTag, "Wi-Fi unavailable for this boot; UART remains active");
    }

    FlexBms::UartV1::StreamDecoder decoder;
    FlexBms::UartV1::Frame frame{};
    std::array<uint8_t, 128U> receiveBuffer{};
    int64_t nextHeartbeatUs = esp_timer_get_time();
    lastValidFrameUs = nextHeartbeatUs;

    while (true)
    {
        const int received = uart_read_bytes(kBmsUart, receiveBuffer.data(), receiveBuffer.size(), pdMS_TO_TICKS(50));
        for (int index = 0; index < received; ++index)
        {
            if (decoder.consume(receiveBuffer[static_cast<size_t>(index)], frame))
            {
                lastValidFrameUs = esp_timer_get_time();
                if (!linkWasHealthy)
                {
                    linkWasHealthy = true;
                    ESP_LOGI(kLogTag, "STM32 UART link healthy");
                }
                logTelemetry(frame);
            }
        }

        const int64_t nowUs = esp_timer_get_time();
        if (nowUs >= nextHeartbeatUs)
        {
            sendHeartbeat();
            nextHeartbeatUs = nowUs + kHeartbeatPeriodUs;
        }
        const bool linkTimedOut = nowUs - lastValidFrameUs >= kGatewayLossUs;
        if (linkTimedOut && !uartLinkTimedOut)
        {
            linkWasHealthy = false;
            ESP_LOGW(kLogTag, "STM32 UART link lost: no CRC-valid frame for 1.5 s");
        }
        uartLinkTimedOut = linkTimedOut;

        statusLed.setUartLinkLost(uartLinkTimedOut);
        statusLed.setWifiWaiting(FlexBms::Wifi::getState() != FlexBms::Wifi::State::Connected);
        // MQTT, OTA and fatal-source integration remain explicit hooks; no
        // network service is introduced merely to select an LED pattern.
        statusLed.setMqttUnavailable(false);
        statusLed.setFirmwareUpdateActive(false);
        statusLed.setFatalLocalFailure(false);
        statusLed.update();
    }
}

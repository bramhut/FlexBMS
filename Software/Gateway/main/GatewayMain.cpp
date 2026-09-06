#include "flexbms/Protocol.h"
#include "flexbms/GatewayApi.h"
#include "flexbms/FirmwareUpdate.h"
#include "flexbms/MqttClient.h"
#include "flexbms/StatusLed.h"
#include "flexbms/TimeSync.h"
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
    bool bmsUartReady = false;

    bool configureUart()
    {
        uart_config_t config{};
        config.baud_rate = kBaudRate;
        config.data_bits = UART_DATA_8_BITS;
        config.parity = UART_PARITY_DISABLE;
        config.stop_bits = UART_STOP_BITS_1;
        config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
        config.rx_flow_ctrl_thresh = 0;
        config.source_clk = UART_SCLK_DEFAULT;
        esp_err_t result = uart_param_config(kBmsUart, &config);
        if (result != ESP_OK)
        {
            ESP_LOGE(kLogTag, "UART setup unavailable; keeping network recovery active: %s", esp_err_to_name(result));
            return false;
        }
        result = uart_set_pin(kBmsUart, kBmsTxGpio, kBmsRxGpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        if (result != ESP_OK)
        {
            ESP_LOGE(kLogTag, "UART pins unavailable; keeping network recovery active: %s", esp_err_to_name(result));
            return false;
        }
        result = uart_driver_install(kBmsUart, 2048, 2048, 0, nullptr, 0);
        if (result != ESP_OK)
        {
            ESP_LOGE(kLogTag, "UART driver unavailable; keeping network recovery active: %s", esp_err_to_name(result));
            return false;
        }
        return true;
    }

    void sendHeartbeat()
    {
        if (!bmsUartReady) return;
        FlexBms::UartV1::Frame heartbeat{.type = FlexBms::UartV1::MessageType::Heartbeat, .sequence = 0U, .length = 0U};
        std::array<uint8_t, FlexBms::UartV1::kMaxFrameBytes> bytes{};
        const size_t length = FlexBms::UartV1::encode(heartbeat, bytes.data(), bytes.size());
        if (length == 0U || uart_write_bytes(kBmsUart, bytes.data(), length) != static_cast<int>(length))
        {
            ESP_LOGW(kLogTag, "UART heartbeat transmit failed");
        }
    }

    bool sendService(FlexBms::GatewayApi::Service service, const uint8_t *arguments, uint8_t argumentLength, uint8_t sequence)
    {
        if (!bmsUartReady || argumentLength > FlexBms::UartV1::kMaxPayloadBytes - 1U)
        {
            return false;
        }
        FlexBms::UartV1::Frame frame{.type = FlexBms::UartV1::MessageType::ServiceRequest, .sequence = sequence, .length = static_cast<uint16_t>(argumentLength + 1U)};
        frame.payload[0] = static_cast<uint8_t>(service);
        for (uint8_t index = 0U; index < argumentLength; ++index)
        {
            frame.payload[index + 1U] = arguments[index];
        }
        std::array<uint8_t, FlexBms::UartV1::kMaxFrameBytes> bytes{};
        const size_t length = FlexBms::UartV1::encode(frame, bytes.data(), bytes.size());
        return length != 0U && uart_write_bytes(kBmsUart, bytes.data(), length) == static_cast<int>(length);
    }

    void completeTimeSync(FlexBms::GatewayApi::ServiceResult result)
    {
        FlexBms::TimeSync::completeStm32Sync(result == FlexBms::GatewayApi::ServiceResult::Ok);
    }

    void completeMqttRunRequest(FlexBms::GatewayApi::ServiceResult)
    {
        // STATUS is the only authoritative state response. MQTT publishes it
        // after the STM32 has processed the named service.
    }

    bool sendMqttRunRequest(bool requested)
    {
        const uint8_t argument = requested ? 1U : 0U;
        return FlexBms::GatewayApi::beginInternalService(FlexBms::GatewayApi::Service::SetRunRequest, &argument, 1U, completeMqttRunRequest);
    }

    void handleServiceResponse(const FlexBms::UartV1::Frame &frame)
    {
        if (frame.length < 2U)
        {
            return;
        }
        FlexBms::GatewayApi::ServiceResult result = FlexBms::GatewayApi::ServiceResult::TransportError;
        if (frame.payload[1] == 0U) result = FlexBms::GatewayApi::ServiceResult::Ok;
        else if (frame.payload[1] == 1U) result = FlexBms::GatewayApi::ServiceResult::Denied;
        else if (frame.payload[1] == 2U) result = FlexBms::GatewayApi::ServiceResult::Invalid;
        else if (frame.payload[1] == 3U) result = FlexBms::GatewayApi::ServiceResult::Busy;
        else if (frame.payload[1] == 4U) result = FlexBms::GatewayApi::ServiceResult::UsbHostActive;
        FlexBms::GatewayApi::completeService(frame.sequence, result, frame.payload.data() + 2U, static_cast<uint8_t>(frame.length - 2U));
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
                ESP_LOGI(kLogTag, "STATUS bms=%u hv=%u slaves=%u flags=0x%04X active=0x%08" PRIX32,
                         status.bmsState, status.hvState, status.slaveCount, status.flags, status.bmsActiveErrors);
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

}

extern "C" void app_main(void)
{
    if (!FlexBms::UartV1::verifyCodec())
    {
        ESP_LOGE(kLogTag, "UART v1 codec self-test failed; continuing with network recovery available");
    }
    if (!FlexBms::GatewayApi::verifyBrowserApi())
    {
        ESP_LOGE(kLogTag, "Gateway browser API self-test failed; continuing with firmware recovery available");
    }

    const bool wifiStarted = FlexBms::Wifi::start();
    if (!wifiStarted)
    {
        ESP_LOGW(kLogTag, "Wi-Fi unavailable for this boot; UART remains active");
    }
    else if (!FlexBms::TimeSync::start())
    {
        ESP_LOGW(kLogTag, "NTP time synchronisation unavailable for this boot");
    }

    const bool gatewayApiStarted = FlexBms::GatewayApi::start(sendService);
    const bool mqttStarted = FlexBms::Mqtt::start(sendMqttRunRequest);
    bmsUartReady = configureUart();
    if (bmsUartReady)
    {
        ESP_LOGI(kLogTag, "UART v1 ready: UART1, GPIO2 TX -> STM32 PA10, GPIO3 RX <- STM32 PA9");
    }
    FlexBms::StatusLed statusLed;
    statusLed.setup();

    FlexBms::UartV1::StreamDecoder decoder;
    FlexBms::UartV1::Frame frame{};
    std::array<uint8_t, 128U> receiveBuffer{};
    int64_t nextHeartbeatUs = esp_timer_get_time();
    lastValidFrameUs = nextHeartbeatUs;
    bool gatewayBootConfirmed = !FlexBms::FirmwareUpdate::isGatewayBootPendingVerification();
    const int64_t gatewayBootConfirmationDeadlineUs = esp_timer_get_time() + 60'000'000LL;
    if (!gatewayApiStarted)
    {
        ESP_LOGE(kLogTag, "Gateway HTTP/WebSocket service failed to start");
    }
    if (!wifiStarted || !gatewayApiStarted || !mqttStarted)
    {
        FlexBms::FirmwareUpdate::restartPendingGatewayImage();
    }

    while (true)
    {
        if (bmsUartReady && !FlexBms::FirmwareUpdate::ownsUart())
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
                    FlexBms::FirmwareUpdate::onFrame(frame);
                    if (frame.type == FlexBms::UartV1::MessageType::Status)
                    {
                        FlexBms::UartV1::Status status{};
                        if (FlexBms::UartV1::decodeStatus(frame, status))
                        {
                            statusLed.synchronizeToStm32Uptime(status.uptimeMs);
                        }
                    }
                    logTelemetry(frame);
                    FlexBms::GatewayApi::publishFrame(frame, static_cast<uint32_t>(esp_timer_get_time() / 1000));
                    FlexBms::Mqtt::publishFrame(frame, static_cast<uint32_t>(esp_timer_get_time() / 1000));
                    if (frame.type == FlexBms::UartV1::MessageType::ServiceResponse)
                    {
                        handleServiceResponse(frame);
                    }
                }
            }
        }

        const int64_t nowUs = esp_timer_get_time();
        if (bmsUartReady && !FlexBms::FirmwareUpdate::ownsUart() && nowUs >= nextHeartbeatUs)
        {
            sendHeartbeat();
            nextHeartbeatUs = nowUs + kHeartbeatPeriodUs;
        }
        const bool linkTimedOut = bmsUartReady && !FlexBms::FirmwareUpdate::ownsUart() && nowUs - lastValidFrameUs >= kGatewayLossUs;
        if (linkTimedOut && !uartLinkTimedOut)
        {
            linkWasHealthy = false;
            ESP_LOGW(kLogTag, "STM32 UART link lost: no CRC-valid frame for 1.5 s");
        }
        uartLinkTimedOut = linkTimedOut;
        FlexBms::GatewayApi::setUartHealthy(!uartLinkTimedOut && linkWasHealthy);
        FlexBms::Mqtt::setUartHealthy(!uartLinkTimedOut && linkWasHealthy);

        FlexBms::Wifi::tick();
        FlexBms::Mqtt::tick(FlexBms::Wifi::getState() == FlexBms::Wifi::State::Connected);
        if (FlexBms::Mqtt::consumeStatusChanged()) FlexBms::GatewayApi::publishGatewayStatus();
        if (!gatewayBootConfirmed && FlexBms::Wifi::getState() == FlexBms::Wifi::State::Connected)
        {
            // A pending OTA image is trusted only once the LAN Companion is
            // reachable again, not merely after the Wi-Fi driver initialises.
            FlexBms::FirmwareUpdate::markGatewayBootHealthy();
            gatewayBootConfirmed = true;
        }
        if (!gatewayBootConfirmed && nowUs >= gatewayBootConfirmationDeadlineUs)
        {
            ESP_LOGE(kLogTag, "Gateway did not regain station connectivity after OTA; rolling back pending image");
            FlexBms::FirmwareUpdate::restartPendingGatewayImage();
        }
        FlexBms::TimeSync::setStationConnected(FlexBms::Wifi::getState() == FlexBms::Wifi::State::Connected);
        if (FlexBms::Wifi::consumeStatusChanged())
        {
            FlexBms::GatewayApi::publishGatewayStatus();
        }
        uint32_t pendingUnixTime = 0U;
        if (!FlexBms::FirmwareUpdate::ownsUart() && FlexBms::TimeSync::pendingStm32Sync(pendingUnixTime))
        {
            const std::array<uint8_t, 4U> timeArguments = {
                static_cast<uint8_t>(pendingUnixTime),
                static_cast<uint8_t>(pendingUnixTime >> 8U),
                static_cast<uint8_t>(pendingUnixTime >> 16U),
                static_cast<uint8_t>(pendingUnixTime >> 24U),
            };
            if (FlexBms::GatewayApi::beginInternalService(FlexBms::GatewayApi::Service::SetRtc, timeArguments.data(),
                                                          timeArguments.size(), completeTimeSync))
            {
                FlexBms::TimeSync::stm32SyncStarted(pendingUnixTime);
            }
        }
        if (FlexBms::TimeSync::consumeStatusChanged())
        {
            FlexBms::GatewayApi::publishGatewayStatus();
        }
        FlexBms::FirmwareUpdate::poll();
        if (FlexBms::FirmwareUpdate::consumeFramedUartReset())
        {
            decoder.reset();
            linkWasHealthy = false;
            uartLinkTimedOut = true;
            lastValidFrameUs = esp_timer_get_time();
        }
        if (FlexBms::FirmwareUpdate::consumeStatusChanged())
        {
            FlexBms::GatewayApi::publishGatewayStatus();
        }
        FlexBms::GatewayApi::poll();

        statusLed.setUartLinkLost(uartLinkTimedOut);
        statusLed.setWifiWaiting(FlexBms::Wifi::getState() != FlexBms::Wifi::State::Connected);
        // The LED reflects the actual MQTT connection without making the
        // network path a BMS safety dependency.
        statusLed.setMqttUnavailable(FlexBms::Wifi::getState() == FlexBms::Wifi::State::Connected && FlexBms::Mqtt::getState() != FlexBms::Mqtt::State::Connected);
        statusLed.setFirmwareUpdateActive(FlexBms::FirmwareUpdate::getStatus().phase == FlexBms::FirmwareUpdate::Phase::Uploading ||
                                          FlexBms::FirmwareUpdate::getStatus().phase == FlexBms::FirmwareUpdate::Phase::Installing);
        statusLed.setFatalLocalFailure(false);
        statusLed.update();
    }
}

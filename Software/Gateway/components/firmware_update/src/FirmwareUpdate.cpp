#include "flexbms/FirmwareUpdate.h"

#include "flexbms/WifiManager.h"

#include "esp_ota_ops.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace FlexBms::FirmwareUpdate
{
    namespace
    {
        constexpr uart_port_t kUart = UART_NUM_1;
        constexpr int kNormalBaud = 1'000'000;
        constexpr int kRomBaud = 115200;
        constexpr uint32_t kStm32FlashAddress = 0x08000000UL;
        // The STM32 ROM GET ID command reports the device product ID, not the
        // bootloader revision. STM32G491 reports PID 0x0479.
        constexpr uint16_t kStm32ProductId = 0x0479U;
        constexpr uint32_t kStm32MaxBytes = 508U * 1024U;
        constexpr uint16_t kStm32ApplicationPages = 254U;
        constexpr int kShortTimeoutMs = 500;
        constexpr int kEraseTimeoutMs = 20'000;
        constexpr int64_t kStm32HandoffTimeoutUs = 3'000'000;
        constexpr int64_t kStm32RomStartDelayUs = 200'000;
        constexpr int64_t kStm32HeartbeatTimeoutUs = 10'000'000;
        constexpr uint8_t kAck = 0x79U;

        enum class Stm32Operation : uint8_t { Idle, RomBootloader, Erase, Program, Verify, StartApplication, AwaitHeartbeat };

        Status status{};
        uint32_t expectedCrc = 0U;
        uint32_t runningCrc = 0xFFFFFFFFU;
        esp_ota_handle_t otaHandle = 0U;
        const esp_partition_t *stagedPartition = nullptr;
        bool uploadOpen = false;
        bool uartOwned = false;
        bool resetFramedUart = false;
        bool stm32HandoffSent = false;
        bool stm32CommitSent = false;
        bool stm32RomStarted = false;
        bool stm32AwaitingHeartbeat = false;
        bool stm32EraseMayHaveStarted = false;
        Stm32Operation stm32Operation = Stm32Operation::Idle;
        bool statusChanged = false;
        int64_t restartAtUs = 0;
        int64_t stm32HandoffDeadlineUs = 0;
        int64_t stm32RomReadyAtUs = 0;
        int64_t stm32HeartbeatDeadlineUs = 0;
        uint32_t expectedStm32Version = 0U;
        uint32_t stm32Offset = 0U;
        uint32_t stm32PaddedBytes = 0U;
        uint32_t stm32ReadbackCrc = 0xFFFFFFFFU;
        char version[48]{};
        char detail[96]{};

        uint32_t crc32Update(uint32_t crc, const uint8_t *data, size_t length)
        {
            for (size_t index = 0U; index < length; ++index)
            {
                crc ^= data[index];
                for (uint8_t bit = 0U; bit < 8U; ++bit)
                {
                    crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? 0xEDB88320U : 0U);
                }
            }
            return crc;
        }

        bool parseStm32Version(const char *text, uint32_t &packed)
        {
            if (text == nullptr) return false;
            uint32_t components[3]{};
            const char *cursor = text;
            for (uint8_t component = 0U; component < 3U; ++component)
            {
                if (*cursor < '0' || *cursor > '9') return false;
                uint32_t value = 0U;
                do
                {
                    value = value * 10U + static_cast<uint32_t>(*cursor - '0');
                    if (value > 255U) return false;
                    ++cursor;
                } while (*cursor >= '0' && *cursor <= '9');
                components[component] = value;
                if (component < 2U)
                {
                    if (*cursor != '.') return false;
                    ++cursor;
                }
            }
            // STM32 stores the numeric major.minor.patch portion. The release
            // bundle may still use a SemVer prerelease/build suffix.
            if (*cursor != '\0' && *cursor != '-' && *cursor != '+') return false;
            packed = components[0] | (components[1] << 8U) | (components[2] << 16U);
            return true;
        }

        void setStatus(Phase phase, Stage stage, const char *message)
        {
            status.phase = phase;
            status.stage = stage;
            std::strncpy(detail, message, sizeof(detail) - 1U);
            detail[sizeof(detail) - 1U] = '\0';
            status.detail = detail;
            statusChanged = true;
        }

        void setProgress(uint32_t bytes)
        {
            const uint32_t bounded = std::min(bytes, status.bytesExpected);
            if (bounded == status.progressBytes) return;
            const uint32_t previousPercent = status.bytesExpected == 0U ? 0U : (status.progressBytes * 100U) / status.bytesExpected;
            status.progressBytes = bounded;
            const uint32_t currentPercent = status.bytesExpected == 0U ? 0U : (status.progressBytes * 100U) / status.bytesExpected;
            if (currentPercent != previousPercent) statusChanged = true;
        }

        void configureNormalUart()
        {
            uart_config_t config{};
            config.baud_rate = kNormalBaud;
            config.data_bits = UART_DATA_8_BITS;
            config.parity = UART_PARITY_DISABLE;
            config.stop_bits = UART_STOP_BITS_1;
            config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
            config.source_clk = UART_SCLK_DEFAULT;
            (void)uart_param_config(kUart, &config);
            (void)uart_flush_input(kUart);
        }

        void fail(const char *message)
        {
            if (otaHandle != 0U)
            {
                (void)esp_ota_abort(otaHandle);
                otaHandle = 0U;
            }
            uploadOpen = false;
            stm32RomStarted = false;
            stm32AwaitingHeartbeat = false;
            stm32HandoffSent = false;
            stm32CommitSent = false;
            stm32Operation = Stm32Operation::Idle;
            stm32HandoffDeadlineUs = 0;
            stm32RomReadyAtUs = 0;
            stm32HeartbeatDeadlineUs = 0;
            if (uartOwned)
            {
                configureNormalUart();
                uartOwned = false;
                resetFramedUart = true;
            }
            if (stm32EraseMayHaveStarted && status.target == Target::Stm32)
            {
                setStatus(Phase::Failed, status.stage, "STM32 update failed after erase; wired recovery required");
                return;
            }
            setStatus(Phase::Failed, status.stage, message);
        }

        bool writeBytes(const uint8_t *data, size_t length)
        {
            return uart_write_bytes(kUart, data, length) == static_cast<int>(length) &&
                   uart_wait_tx_done(kUart, pdMS_TO_TICKS(kShortTimeoutMs)) == ESP_OK;
        }

        bool readByte(uint8_t &value, int timeoutMs)
        {
            return uart_read_bytes(kUart, &value, 1U, pdMS_TO_TICKS(timeoutMs)) == 1;
        }

        bool expectAck(int timeoutMs = kShortTimeoutMs)
        {
            uint8_t response = 0U;
            return readByte(response, timeoutMs) && response == kAck;
        }

        bool command(uint8_t commandByte, int timeoutMs = kShortTimeoutMs)
        {
            const uint8_t bytes[] = {commandByte, static_cast<uint8_t>(commandByte ^ 0xFFU)};
            return writeBytes(bytes, sizeof(bytes)) && expectAck(timeoutMs);
        }

        bool address(uint32_t value)
        {
            std::array<uint8_t, 5U> bytes = {
                static_cast<uint8_t>(value >> 24U), static_cast<uint8_t>(value >> 16U),
                static_cast<uint8_t>(value >> 8U), static_cast<uint8_t>(value), 0U};
            bytes[4] = static_cast<uint8_t>(bytes[0] ^ bytes[1] ^ bytes[2] ^ bytes[3]);
            return writeBytes(bytes.data(), bytes.size()) && expectAck();
        }

        bool verifyStm32ProductId()
        {
            if (!command(0x02U)) return false;
            uint8_t length = 0U;
            if (!readByte(length, kShortTimeoutMs) || length != 1U) return false;
            uint8_t high = 0U;
            uint8_t low = 0U;
            return readByte(high, kShortTimeoutMs) && readByte(low, kShortTimeoutMs) && expectAck() &&
                   static_cast<uint16_t>((static_cast<uint16_t>(high) << 8U) | low) == kStm32ProductId;
        }

        bool eraseStm32()
        {
            if (!command(0x44U)) return false;
            // Extended erase takes N - 1 followed by N page numbers.  The
            // final two 2 KiB pages are reserved for runtime configuration.
            std::array<uint8_t, 2U + 2U * kStm32ApplicationPages + 1U> pages{};
            const uint16_t countMinusOne = kStm32ApplicationPages - 1U;
            pages[0] = static_cast<uint8_t>(countMinusOne >> 8U);
            pages[1] = static_cast<uint8_t>(countMinusOne);
            uint8_t checksum = pages[0] ^ pages[1];
            for (uint16_t page = 0U; page < kStm32ApplicationPages; ++page)
            {
                const size_t offset = 2U + static_cast<size_t>(page) * 2U;
                pages[offset] = static_cast<uint8_t>(page >> 8U);
                pages[offset + 1U] = static_cast<uint8_t>(page);
                checksum ^= pages[offset] ^ pages[offset + 1U];
            }
            pages.back() = checksum;
            return writeBytes(pages.data(), pages.size()) && expectAck(kEraseTimeoutMs);
        }

        bool writeStm32Block(uint32_t addressValue, const uint8_t *data, size_t length)
        {
            if (length == 0U || length > 256U || !command(0x31U) || !address(addressValue)) return false;
            std::array<uint8_t, 258U> packet{};
            packet[0] = static_cast<uint8_t>(length - 1U);
            uint8_t checksum = packet[0];
            for (size_t index = 0U; index < length; ++index)
            {
                packet[index + 1U] = data[index];
                checksum ^= data[index];
            }
            packet[length + 1U] = checksum;
            return writeBytes(packet.data(), length + 2U) && expectAck();
        }

        bool readStm32Block(uint32_t addressValue, uint8_t *data, size_t length)
        {
            if (length == 0U || length > 256U || !command(0x11U) || !address(addressValue)) return false;
            const uint8_t count[] = {static_cast<uint8_t>(length - 1U), static_cast<uint8_t>((length - 1U) ^ 0xFFU)};
            return writeBytes(count, sizeof(count)) && expectAck() &&
                   uart_read_bytes(kUart, data, length, pdMS_TO_TICKS(kShortTimeoutMs)) == static_cast<int>(length);
        }

        bool goStm32()
        {
            return command(0x21U) && address(kStm32FlashAddress);
        }

        bool enterStm32RomBootloader()
        {
            uart_config_t config{};
            config.baud_rate = kRomBaud;
            config.data_bits = UART_DATA_8_BITS;
            config.parity = UART_PARITY_EVEN;
            config.stop_bits = UART_STOP_BITS_1;
            config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
            config.source_clk = UART_SCLK_DEFAULT;
            if (uart_param_config(kUart, &config) != ESP_OK) return false;
            (void)uart_flush_input(kUart);
            const uint8_t sync = 0x7FU;
            return writeBytes(&sync, 1U) && expectAck() && verifyStm32ProductId();
        }

        bool sendFramedRequest(uint8_t serviceId, uint8_t sequence, const uint8_t *arguments, uint8_t argumentLength)
        {
            std::array<uint8_t, UartV1::kMaxFrameBytes> bytes{};
            if (argumentLength > UartV1::kMaxPayloadBytes - 1U) return false;
            UartV1::Frame frame{.type = UartV1::MessageType::ServiceRequest, .sequence = sequence, .length = static_cast<uint16_t>(argumentLength + 1U)};
            frame.payload[0] = serviceId;
            if (argumentLength != 0U) std::memcpy(frame.payload.data() + 1U, arguments, argumentLength);
            const size_t length = UartV1::encode(frame, bytes.data(), bytes.size());
            return length != 0U && uart_write_bytes(kUart, bytes.data(), length) == static_cast<int>(length) &&
                   uart_wait_tx_done(kUart, pdMS_TO_TICKS(kShortTimeoutMs)) == ESP_OK;
        }

        bool sendPrepareRequest()
        {
            std::array<uint8_t, 12U> arguments{};
            for (uint8_t index = 0U; index < 4U; ++index)
            {
                arguments[index] = static_cast<uint8_t>(expectedStm32Version >> (index * 8U));
                arguments[4U + index] = static_cast<uint8_t>(status.bytesExpected >> (index * 8U));
                arguments[8U + index] = static_cast<uint8_t>(expectedCrc >> (index * 8U));
            }
            return sendFramedRequest(0x07U, 0xFEU, arguments.data(), arguments.size());
        }
    }

    bool isAvailable()
    {
        // The Gateway image must remain recoverable from the local setup AP.
        // STM32 updates are restricted separately in isAvailable(Target),
        // because they require the station LAN and a working BMS UART path.
        return (Wifi::allowsBmsServices() || Wifi::isAccessPointActive()) && !uploadOpen && status.phase != Phase::Installing;
    }

    bool isAvailable(Target target)
    {
        if (!isAvailable()) return false;
        if (target == Target::Stm32 && (Wifi::isAccessPointActive() || !Wifi::allowsBmsServices() || stm32EraseMayHaveStarted)) return false;
        return true;
    }
    const Status &getStatus() { return status; }
    bool consumeStatusChanged() { const bool changed = statusChanged; statusChanged = false; return changed; }
    bool ownsUart() { return uartOwned; }
    bool consumeFramedUartReset() { const bool value = resetFramedUart; resetFramedUart = false; return value; }

    bool beginUpload(Target target, const char *requestedVersion, uint32_t bytes, uint32_t crc32)
    {
        if (!isAvailable(target) || requestedVersion == nullptr || bytes == 0U || (target == Target::Stm32 && bytes > kStm32MaxBytes)) return false;
        if (target == Target::Stm32 && !parseStm32Version(requestedVersion, expectedStm32Version)) return false;
        stm32HandoffSent = false;
        stm32CommitSent = false;
        stm32RomStarted = false;
        stm32AwaitingHeartbeat = false;
        stm32Operation = Stm32Operation::Idle;
        stm32HandoffDeadlineUs = 0;
        stm32RomReadyAtUs = 0;
        stm32HeartbeatDeadlineUs = 0;
        std::strncpy(version, requestedVersion, sizeof(version) - 1U);
        version[sizeof(version) - 1U] = '\0';
        status = {.phase = Phase::Uploading, .target = target, .stage = Stage::Upload, .bytesReceived = 0U, .bytesExpected = bytes,
                  .progressBytes = 0U, .version = version, .detail = detail};
        expectedCrc = crc32;
        runningCrc = 0xFFFFFFFFU;
        uploadOpen = true;
        setStatus(Phase::Uploading, Stage::Upload, "Uploading image");
        if (target == Target::Gateway)
        {
            const esp_partition_t *partition = esp_ota_get_next_update_partition(nullptr);
            if (partition == nullptr || bytes > partition->size || esp_ota_begin(partition, bytes, &otaHandle) != ESP_OK) { fail("Gateway OTA start failed"); return false; }
        }
        else
        {
            stagedPartition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, static_cast<esp_partition_subtype_t>(0x40U), "bms_update");
            if (stagedPartition == nullptr || bytes > stagedPartition->size || esp_partition_erase_range(stagedPartition, 0U, stagedPartition->size) != ESP_OK)
            { fail("STM32 staging unavailable"); return false; }
        }
        return true;
    }

    bool writeUpload(const uint8_t *data, size_t bytes)
    {
        if (!uploadOpen || data == nullptr || bytes == 0U || bytes > status.bytesExpected - status.bytesReceived) return false;
        const esp_err_t result = status.target == Target::Gateway ? esp_ota_write(otaHandle, data, bytes) : esp_partition_write(stagedPartition, status.bytesReceived, data, bytes);
        if (result != ESP_OK) { fail("Image write failed"); return false; }
        runningCrc = crc32Update(runningCrc, data, bytes);
        status.bytesReceived += static_cast<uint32_t>(bytes);
        setProgress(status.bytesReceived);
        return true;
    }

    bool finishUpload()
    {
        if (!uploadOpen || status.bytesReceived != status.bytesExpected || (runningCrc ^ 0xFFFFFFFFU) != expectedCrc) { fail("Image CRC or length mismatch"); return false; }
        uploadOpen = false;
        if (status.target == Target::Gateway)
        {
            const esp_partition_t *partition = esp_ota_get_next_update_partition(nullptr);
            if (otaHandle == 0U || esp_ota_end(otaHandle) != ESP_OK || esp_ota_set_boot_partition(partition) != ESP_OK) { otaHandle = 0U; fail("Gateway OTA validation failed"); return false; }
            otaHandle = 0U;
            setStatus(Phase::Installing, Stage::Restart, "Restarting Gateway");
            restartAtUs = esp_timer_get_time() + 1'000'000;
        }
        else
        {
            setStatus(Phase::Installing, Stage::Handoff, "Waiting for STM32 safe-off handoff");
        }
        return true;
    }

    void abortUpload(const char *message) { fail(message); }

    void onFrame(const UartV1::Frame &frame)
    {
        if (stm32EraseMayHaveStarted && frame.type == UartV1::MessageType::Heartbeat)
        {
            // A heartbeat proves that a valid application is running again,
            // for example after a manual recovery flash.
            stm32EraseMayHaveStarted = false;
        }
        if (status.target != Target::Stm32 || status.phase != Phase::Installing) return;
        if (frame.type == UartV1::MessageType::ServiceResponse && frame.sequence == 0xFEU && frame.length >= 2U && frame.payload[0] == 0x07U)
        {
            stm32HandoffDeadlineUs = 0;
            if (frame.payload[1] == 4U) { fail("Disconnect STM32 USB data connection before updating"); return; }
            if (frame.payload[1] != 0U) { fail("STM32 denied update handoff"); return; }
            if (!sendFramedRequest(0x0AU, 0xFEU, nullptr, 0U)) { fail("STM32 handoff commit send failed"); return; }
            stm32CommitSent = true;
            uartOwned = true;
            stm32RomStarted = true;
            stm32RomReadyAtUs = esp_timer_get_time() + kStm32RomStartDelayUs;
            setStatus(Phase::Installing, Stage::RomBootloader, "Starting STM32 ROM bootloader");
        }
        else if (stm32AwaitingHeartbeat && frame.type == UartV1::MessageType::Heartbeat)
        {
            uartOwned = false;
            stm32AwaitingHeartbeat = false;
            stm32HeartbeatDeadlineUs = 0;
            stm32Operation = Stm32Operation::Idle;
            stm32EraseMayHaveStarted = false;
            setProgress(status.bytesExpected);
            setStatus(Phase::Complete, Stage::Complete, "STM32 update complete");
        }
    }

    void poll()
    {
        if (status.target == Target::Gateway && status.phase == Phase::Installing && restartAtUs != 0 && esp_timer_get_time() >= restartAtUs)
        {
            esp_restart();
        }
        if (status.target != Target::Stm32 || status.phase != Phase::Installing) return;
        if (!stm32HandoffSent)
        {
            stm32HandoffSent = true;
            stm32HandoffDeadlineUs = esp_timer_get_time() + kStm32HandoffTimeoutUs;
            if (!sendPrepareRequest()) fail("STM32 handoff prepare send failed");
            return;
        }
        if (!stm32RomStarted && stm32HandoffDeadlineUs != 0 && esp_timer_get_time() >= stm32HandoffDeadlineUs)
        {
            fail("STM32 handoff response timed out");
            return;
        }
        if (stm32AwaitingHeartbeat)
        {
            if (esp_timer_get_time() >= stm32HeartbeatDeadlineUs)
            {
                fail("STM32 application heartbeat timed out after Go");
            }
            return;
        }
        if (stm32RomStarted)
        {
            if (esp_timer_get_time() < stm32RomReadyAtUs) return;
            stm32RomStarted = false;
            stm32RomReadyAtUs = 0;
            stm32Operation = Stm32Operation::RomBootloader;
            setStatus(Phase::Installing, Stage::RomBootloader, "Synchronising STM32 ROM bootloader");
            return;
        }

        switch (stm32Operation)
        {
        case Stm32Operation::RomBootloader:
            if (!enterStm32RomBootloader()) { fail("STM32 ROM transfer failed"); return; }
            stm32Operation = Stm32Operation::Erase;
            setStatus(Phase::Installing, Stage::Erase, "Erasing STM32 flash");
            return;

        case Stm32Operation::Erase:
            stm32EraseMayHaveStarted = true;
            if (!eraseStm32()) { fail("STM32 ROM transfer failed"); return; }
            stm32Offset = 0U;
            stm32PaddedBytes = (status.bytesExpected + 7U) & ~7U;
            status.progressBytes = 0U;
            stm32Operation = Stm32Operation::Program;
            setStatus(Phase::Installing, Stage::Program, "Programming STM32");
            return;

        case Stm32Operation::Program:
        {
            if (stm32Offset >= stm32PaddedBytes)
            {
                stm32Offset = 0U;
                stm32ReadbackCrc = 0xFFFFFFFFU;
                status.progressBytes = 0U;
                stm32Operation = Stm32Operation::Verify;
                setStatus(Phase::Installing, Stage::Verify, "Verifying STM32 image");
                return;
            }
            std::array<uint8_t, 256U> block{};
            const size_t count = std::min<size_t>(block.size(), stm32PaddedBytes - stm32Offset);
            std::fill(block.begin(), block.begin() + count, 0xFFU);
            const size_t sourceCount = std::min<size_t>(count, status.bytesExpected - std::min(stm32Offset, status.bytesExpected));
            if (sourceCount != 0U && esp_partition_read(stagedPartition, stm32Offset, block.data(), sourceCount) != ESP_OK) { fail("STM32 staged image read failed"); return; }
            if (!writeStm32Block(kStm32FlashAddress + stm32Offset, block.data(), count)) { fail("STM32 ROM transfer failed"); return; }
            stm32Offset += static_cast<uint32_t>(count);
            setProgress(stm32Offset);
            return;
        }

        case Stm32Operation::Verify:
        {
            if (stm32Offset >= status.bytesExpected)
            {
                if ((stm32ReadbackCrc ^ 0xFFFFFFFFU) != expectedCrc) { fail("STM32 ROM readback CRC mismatch"); return; }
                stm32Operation = Stm32Operation::StartApplication;
                setStatus(Phase::Installing, Stage::Restart, "Restarting STM32 application");
                return;
            }
            std::array<uint8_t, 256U> block{};
            const size_t count = std::min<size_t>(block.size(), status.bytesExpected - stm32Offset);
            if (!readStm32Block(kStm32FlashAddress + stm32Offset, block.data(), count)) { fail("STM32 ROM transfer failed"); return; }
            stm32ReadbackCrc = crc32Update(stm32ReadbackCrc, block.data(), count);
            stm32Offset += static_cast<uint32_t>(count);
            setProgress(stm32Offset);
            return;
        }

        case Stm32Operation::StartApplication:
            if (!goStm32()) { fail("STM32 ROM start application failed"); return; }
            configureNormalUart();
            resetFramedUart = true;
            stm32AwaitingHeartbeat = true;
            stm32Operation = Stm32Operation::AwaitHeartbeat;
            stm32HeartbeatDeadlineUs = esp_timer_get_time() + kStm32HeartbeatTimeoutUs;
            uartOwned = false;
            setStatus(Phase::Installing, Stage::Restart, "Waiting for STM32 heartbeat");
            return;

        default:
            return;
        }
    }

    bool isGatewayBootPendingVerification()
    {
        const esp_partition_t *running = esp_ota_get_running_partition();
        esp_ota_img_states_t imageState{};
        return running != nullptr && esp_ota_get_state_partition(running, &imageState) == ESP_OK && imageState == ESP_OTA_IMG_PENDING_VERIFY;
    }

    void markGatewayBootHealthy()
    {
        if (!isGatewayBootPendingVerification()) return;
        const esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
        if (result != ESP_OK && result != ESP_ERR_NOT_SUPPORTED)
        {
            ESP_LOGW("flexbms_update", "Could not confirm Gateway OTA image: %s", esp_err_to_name(result));
        }
    }

    void restartPendingGatewayImage()
    {
        if (isGatewayBootPendingVerification())
        {
            ESP_LOGE("flexbms_update", "Gateway startup incomplete; rebooting pending image for bootloader rollback");
            esp_restart();
        }
    }
}

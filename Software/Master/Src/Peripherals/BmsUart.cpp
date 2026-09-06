#include "BmsUart.h"

#include "BoardIO.h"
#include "FirmwareVersion.h"
#include "FaultManager.h"
#include "FreeRTOS.h"
#include "RtcTime.h"
#include "cmsis_os.h"
#include "bcc/SlaveController.h"
#include "bcc/bcc_utils.h"
#include "RuntimeConfiguration.h"
#include "main.h"
#include "pcc.h"
#include "queue.h"
#include "task.h"
#include "usb_device.h"
#include "usart.h"
#include "USBCOM.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>

// Defined by the generated USB-device implementation.  A configured device
// means a USB host has enumerated the STM32 CDC interface.
extern USBD_HandleTypeDef hUsbDeviceFS;

namespace BmsUart
{
    namespace
    {
        constexpr uint8_t MAGIC_0 = 0x46U;
        constexpr uint8_t MAGIC_1 = 0x42U;
        constexpr uint8_t VERSION = 0x01U;
        constexpr size_t HEADER_BYTES = 7U;
        constexpr size_t CRC_BYTES = 4U;
        constexpr size_t MAX_PAYLOAD_BYTES = 512U;
        constexpr size_t MAX_FRAME_BYTES = HEADER_BYTES + MAX_PAYLOAD_BYTES + CRC_BYTES;
        constexpr uint32_t HEARTBEAT_PERIOD_MS = 500U;
        constexpr uint32_t SNAPSHOT_PERIOD_MS = 500U;
        constexpr uint32_t GATEWAY_LOSS_MS = 1500U;
        constexpr uint32_t USB_COMPANION_LOSS_MS = 1500U;
        constexpr uint32_t RX_RECOVERY_RETRY_MS = 1000U;
        constexpr uint32_t REGISTER_READ_TIMEOUT_MS = 500U;
        constexpr uint32_t TX_TIMEOUT_MS = 50U;
        constexpr uint32_t APP_FLASH_BYTES = RuntimeConfiguration::APPLICATION_FLASH_BYTES;
        constexpr uintptr_t SYSTEM_MEMORY_BASE = 0x1FFF0000UL;

        enum MessageType : uint8_t
        {
            HEARTBEAT = 0x01U,
            STATUS = 0x02U,
            PACK = 0x03U,
            CELL = 0x04U,
            TEMPERATURE = 0x05U,
            HV_VOLTAGES = 0x06U,
            SERVICE_REQUEST = 0x10U,
            SERVICE_RESPONSE = 0x11U,
            EVENT = 0x12U,
        };

        enum ServiceId : uint8_t
        {
            GET_STATUS = 0x01U,
            SET_RUN_REQUEST = 0x02U,
            ACKNOWLEDGE_FAULTS = 0x03U,
            READ_REGISTER = 0x04U,
            SET_RTC = 0x05U,
            GET_DEVICE_INFO = 0x06U,
            PREPARE_STM32_BOOTLOADER = 0x07U,
            GET_RTC = 0x08U,
            SET_BALANCING_ENABLED = 0x09U,
            COMMIT_STM32_BOOTLOADER = 0x0AU,
            GET_CONFIG = 0x0BU,
            SET_CONFIG = 0x0CU,
            GET_DIAGNOSTIC_REPORT = 0x0DU,
        };

        enum ServiceResult : uint8_t
        {
            OK = 0U,
            DENIED = 1U,
            INVALID = 2U,
            BUSY = 3U,
            USB_HOST_ACTIVE = 4U,
        };

        enum class Link : uint8_t
        {
            Uart,
            Usb,
        };

        struct ReceivedFrame
        {
            Link link = Link::Uart;
            uint8_t type;
            uint8_t sequence;
            uint16_t length;
            std::array<uint8_t, MAX_PAYLOAD_BYTES> payload;
        };

        struct PendingRegisterRead
        {
            bool active = false;
            Link link = Link::Uart;
            uint8_t sequence = 0U;
            uint8_t slaveIndex = 0U;
            uint8_t regAddr = 0U;
            uint32_t deadlineMs = 0U;
        };

        struct EventCache
        {
            bool initialized = false;
            uint8_t bmsState = 0U;
            uint8_t hvState = 0U;
            uint32_t bmsActive = 0U;
            uint32_t bmsLatched = 0U;
            uint32_t hvActive = 0U;
            uint32_t hvLatched = 0U;
            uint32_t warnings = 0U;
            bool measurementsFresh = false;
        };

        enum class RxState : uint8_t
        {
            MAGIC_0,
            MAGIC_1,
            BODY,
            CRC_FIELD,
        };

        constexpr size_t RX_DMA_BYTES = 128U;
        constexpr size_t RX_QUEUE_DEPTH = 3U;

        osThreadId_t uartTaskHandle = nullptr;
        const osThreadAttr_t uartTaskAttributes = {
            .name = "bmsUart",
            .stack_size = 2048U,
            .priority = (osPriority_t)osPriorityNormal,
        };

        StaticQueue_t rxQueueControl;
        std::array<uint8_t, RX_QUEUE_DEPTH * sizeof(ReceivedFrame)> rxQueueStorage = {};
        QueueHandle_t rxQueue = nullptr;

        std::array<uint8_t, RX_DMA_BYTES> rxDmaBuffer = {};
        std::array<uint8_t, MAX_FRAME_BYTES> rxParserBuffer = {};
        std::array<uint8_t, MAX_FRAME_BYTES> txBuffer = {};
        RxState rxState = RxState::MAGIC_0;
        size_t rxParserLength = 0U;
        uint16_t rxPayloadLength = 0U;
        uint8_t rxCrcBytes = 0U;
        std::atomic<bool> txComplete{false};
        std::atomic<bool> hasValidGatewayFrame{false};
        std::atomic<uint32_t> lastValidGatewayFrameMs{0U};
        std::atomic<uint32_t> uartStartedMs{0U};
        std::atomic<uint32_t> lastValidUsbHeartbeatMs{0U};
        std::atomic<bool> rxRecoveryRequested{false};

        // Retained for debugger inspection after a recovered UART incident.
        std::atomic<uint32_t> rxErrorCount{0U};
        std::atomic<uint32_t> lastRxErrorCode{HAL_UART_ERROR_NONE};
        std::atomic<uint32_t> rxArmFailureCount{0U};
        std::atomic<uint32_t> rxRecoveryCount{0U};
        uint32_t nextRxRecoveryAttemptMs = 0U;

        // CDC shares a port with the engineering text console. Hold only
        // bytes beginning a possible FB frame; every other byte continues to
        // the existing console unchanged.
        std::array<uint8_t, MAX_FRAME_BYTES> usbCandidate = {};
        size_t usbCandidateLength = 0U;
        size_t usbCandidateExpectedLength = 0U;

        PendingRegisterRead pendingRegisterRead;
        Link currentResponseLink = Link::Uart;
        EventCache eventCache;
        uint32_t lastSeenMeasurement = 0U;

        uint16_t readLe16(const uint8_t *data)
        {
            return static_cast<uint16_t>(data[0]) |
                   (static_cast<uint16_t>(data[1]) << 8U);
        }

        uint32_t readLe32(const uint8_t *data)
        {
            return static_cast<uint32_t>(data[0]) |
                   (static_cast<uint32_t>(data[1]) << 8U) |
                   (static_cast<uint32_t>(data[2]) << 16U) |
                   (static_cast<uint32_t>(data[3]) << 24U);
        }

        void writeLe16(uint8_t *data, uint16_t value)
        {
            data[0] = static_cast<uint8_t>(value);
            data[1] = static_cast<uint8_t>(value >> 8U);
        }

        void writeLe32(uint8_t *data, uint32_t value)
        {
            data[0] = static_cast<uint8_t>(value);
            data[1] = static_cast<uint8_t>(value >> 8U);
            data[2] = static_cast<uint8_t>(value >> 16U);
            data[3] = static_cast<uint8_t>(value >> 24U);
        }

        uint32_t crc32(const uint8_t *data, size_t length)
        {
            uint32_t crc = 0xFFFFFFFFUL;
            for (size_t i = 0U; i < length; ++i)
            {
                crc ^= data[i];
                for (uint8_t bit = 0U; bit < 8U; ++bit)
                {
                    crc = (crc & 1U) != 0U ? (crc >> 1U) ^ 0xEDB88320UL : (crc >> 1U);
                }
            }
            return crc ^ 0xFFFFFFFFUL;
        }

        void resetParser(uint8_t lastByte = 0U)
        {
            rxParserLength = 0U;
            rxPayloadLength = 0U;
            rxCrcBytes = 0U;
            if (lastByte == MAGIC_0)
            {
                rxParserBuffer[0] = MAGIC_0;
                rxParserLength = 1U;
                rxState = RxState::MAGIC_1;
            }
            else
            {
                rxState = RxState::MAGIC_0;
            }
        }

        bool armReceiveDma()
        {
            if (HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rxDmaBuffer.data(), rxDmaBuffer.size()) == HAL_OK)
            {
                __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT);
                return true;
            }
            rxArmFailureCount.fetch_add(1U, std::memory_order_relaxed);
            rxRecoveryRequested.store(true, std::memory_order_release);
            return false;
        }

        bool gatewayLinkLostAt(uint32_t now)
        {
            return now - uartStartedMs.load(std::memory_order_acquire) >= GATEWAY_LOSS_MS &&
                   (!hasValidGatewayFrame.load(std::memory_order_acquire) ||
                    now - lastValidGatewayFrameMs.load(std::memory_order_acquire) >= GATEWAY_LOSS_MS);
        }

        void processReceiveRecovery(uint32_t now)
        {
            if (!rxRecoveryRequested.load(std::memory_order_acquire) && !gatewayLinkLostAt(now)) return;
            if (static_cast<int32_t>(now - nextRxRecoveryAttemptMs) < 0) return;
            nextRxRecoveryAttemptMs = now + RX_RECOVERY_RETRY_MS;

            // Abort only reception so telemetry transmission remains active.
            // Clear the request before the abort so an ISR-detected error that
            // races this recovery schedules another bounded attempt.
            rxRecoveryRequested.store(false, std::memory_order_release);
            if (HAL_UART_AbortReceive(&huart1) != HAL_OK)
            {
                rxRecoveryRequested.store(true, std::memory_order_release);
                return;
            }

            resetParser();
            if (armReceiveDma())
            {
                rxRecoveryCount.fetch_add(1U, std::memory_order_relaxed);
            }
        }

        void enqueueFrameFromIsr(Link link)
        {
            ReceivedFrame frame = {};
            frame.link = link;
            frame.type = rxParserBuffer[3];
            frame.sequence = rxParserBuffer[4];
            frame.length = rxPayloadLength;
            if (rxPayloadLength != 0U)
            {
                std::memcpy(frame.payload.data(), rxParserBuffer.data() + HEADER_BYTES, rxPayloadLength);
            }

            BaseType_t taskWoken = pdFALSE;
            (void)xQueueSendFromISR(rxQueue, &frame, &taskWoken);
            portYIELD_FROM_ISR(taskWoken);
            if (link == Link::Uart)
            {
                hasValidGatewayFrame.store(true, std::memory_order_release);
                lastValidGatewayFrameMs.store(HAL_GetTick(), std::memory_order_release);
            }
        }

        void parseByteFromIsr(uint8_t byte)
        {
            switch (rxState)
            {
            case RxState::MAGIC_0:
                if (byte == MAGIC_0)
                {
                    rxParserBuffer[0] = byte;
                    rxParserLength = 1U;
                    rxState = RxState::MAGIC_1;
                }
                break;

            case RxState::MAGIC_1:
                if (byte == MAGIC_1)
                {
                    rxParserBuffer[1] = byte;
                    rxParserLength = 2U;
                    rxState = RxState::BODY;
                }
                else
                {
                    resetParser(byte);
                }
                break;

            case RxState::BODY:
                rxParserBuffer[rxParserLength++] = byte;
                if (rxParserLength == HEADER_BYTES)
                {
                    rxPayloadLength = readLe16(rxParserBuffer.data() + 5U);
                    if (rxParserBuffer[2] != VERSION || rxPayloadLength > MAX_PAYLOAD_BYTES)
                    {
                        resetParser(byte);
                    }
                    else if (rxPayloadLength == 0U)
                    {
                        rxState = RxState::CRC_FIELD;
                    }
                }
                else if (rxParserLength == HEADER_BYTES + rxPayloadLength)
                {
                    rxState = RxState::CRC_FIELD;
                }
                break;

            case RxState::CRC_FIELD:
                rxParserBuffer[rxParserLength++] = byte;
                ++rxCrcBytes;
                if (rxCrcBytes == CRC_BYTES)
                {
                    const size_t bodyLength = HEADER_BYTES + rxPayloadLength;
                    const uint32_t receivedCrc = readLe32(rxParserBuffer.data() + bodyLength);
                    if (crc32(rxParserBuffer.data(), bodyLength) == receivedCrc)
                    {
                        enqueueFrameFromIsr(Link::Uart);
                        resetParser();
                    }
                    else
                    {
                        resetParser(byte);
                    }
                }
                break;
            }
        }

        void forwardConsoleByteFromIsr(uint8_t byte)
        {
            (void)USBCOM::rxAddData(&byte, 1U);
        }

        void resetUsbCandidate()
        {
            usbCandidateLength = 0U;
            usbCandidateExpectedLength = 0U;
        }

        void resynchroniseUsbCandidate()
        {
            for (size_t index = 1U; index + 1U < usbCandidateLength; ++index)
            {
                if (usbCandidate[index] != MAGIC_0 || usbCandidate[index + 1U] != MAGIC_1)
                {
                    continue;
                }

                const size_t remaining = usbCandidateLength - index;
                if (remaining >= HEADER_BYTES)
                {
                    const uint16_t payloadLength = readLe16(usbCandidate.data() + index + 5U);
                    if (usbCandidate[index + 2U] != VERSION || payloadLength > MAX_PAYLOAD_BYTES)
                    {
                        continue;
                    }
                    usbCandidateExpectedLength = HEADER_BYTES + payloadLength + CRC_BYTES;
                }
                else
                {
                    usbCandidateExpectedLength = 0U;
                }
                std::memmove(usbCandidate.data(), usbCandidate.data() + index, remaining);
                usbCandidateLength = remaining;
                return;
            }
            const bool endsWithMagic0 = usbCandidateLength != 0U && usbCandidate[usbCandidateLength - 1U] == MAGIC_0;
            resetUsbCandidate();
            if (endsWithMagic0)
            {
                usbCandidate[0] = MAGIC_0;
                usbCandidateLength = 1U;
            }
        }

        void enqueueUsbCandidateFromIsr()
        {
            ReceivedFrame frame = {};
            frame.link = Link::Usb;
            frame.type = usbCandidate[3];
            frame.sequence = usbCandidate[4];
            frame.length = readLe16(usbCandidate.data() + 5U);
            if (frame.length != 0U)
            {
                std::memcpy(frame.payload.data(), usbCandidate.data() + HEADER_BYTES, frame.length);
            }
            BaseType_t taskWoken = pdFALSE;
            (void)xQueueSendFromISR(rxQueue, &frame, &taskWoken);
            portYIELD_FROM_ISR(taskWoken);
        }

        void consumeUsbByteFromIsr(uint8_t byte)
        {
            if (usbCandidateLength == 0U)
            {
                if (byte == MAGIC_0)
                {
                    usbCandidate[0] = byte;
                    usbCandidateLength = 1U;
                }
                else
                {
                    forwardConsoleByteFromIsr(byte);
                }
                return;
            }

            if (usbCandidateLength == 1U && byte != MAGIC_1)
            {
                forwardConsoleByteFromIsr(MAGIC_0);
                resetUsbCandidate();
                consumeUsbByteFromIsr(byte);
                return;
            }

            if (usbCandidateLength >= usbCandidate.size())
            {
                resynchroniseUsbCandidate();
                return;
            }
            usbCandidate[usbCandidateLength++] = byte;
            if (usbCandidateLength == HEADER_BYTES)
            {
                const uint16_t payloadLength = readLe16(usbCandidate.data() + 5U);
                if (usbCandidate[2] != VERSION || payloadLength > MAX_PAYLOAD_BYTES)
                {
                    resynchroniseUsbCandidate();
                    return;
                }
                usbCandidateExpectedLength = HEADER_BYTES + payloadLength + CRC_BYTES;
            }
            if (usbCandidateExpectedLength == 0U || usbCandidateLength < usbCandidateExpectedLength) return;
            if (usbCandidateLength != usbCandidateExpectedLength ||
                crc32(usbCandidate.data(), usbCandidateExpectedLength - CRC_BYTES) != readLe32(usbCandidate.data() + usbCandidateExpectedLength - CRC_BYTES))
            {
                resynchroniseUsbCandidate();
                return;
            }
            enqueueUsbCandidateFromIsr();
            resetUsbCandidate();
        }

        size_t encodeFrame(uint8_t type, uint8_t sequence, const uint8_t *payload, uint16_t payloadLength)
        {
            if (payloadLength > MAX_PAYLOAD_BYTES || (payloadLength != 0U && payload == nullptr))
            {
                return 0U;
            }

            txBuffer[0] = MAGIC_0;
            txBuffer[1] = MAGIC_1;
            txBuffer[2] = VERSION;
            txBuffer[3] = type;
            txBuffer[4] = sequence;
            writeLe16(txBuffer.data() + 5U, payloadLength);
            if (payloadLength != 0U)
            {
                std::memcpy(txBuffer.data() + HEADER_BYTES, payload, payloadLength);
            }

            const size_t bodyLength = HEADER_BYTES + payloadLength;
            writeLe32(txBuffer.data() + bodyLength, crc32(txBuffer.data(), bodyLength));
            return bodyLength + CRC_BYTES;
        }

        bool sendFrameToUart(uint8_t type, uint8_t sequence, const uint8_t *payload, uint16_t payloadLength)
        {
            const size_t frameLength = encodeFrame(type, sequence, payload, payloadLength);
            if (frameLength == 0U) return false;
            txComplete.store(false, std::memory_order_release);
            if (HAL_UART_Transmit_DMA(&huart1, txBuffer.data(), frameLength) != HAL_OK)
            {
                return false;
            }

            const uint32_t deadline = HAL_GetTick() + TX_TIMEOUT_MS;
            while (!txComplete.load(std::memory_order_acquire))
            {
                if (static_cast<int32_t>(HAL_GetTick() - deadline) >= 0)
                {
                    (void)HAL_UART_AbortTransmit(&huart1);
                    return false;
                }
                osDelay(1U);
            }
            return true;
        }

        bool usbCompanionAlive()
        {
            const uint32_t lastHeartbeat = lastValidUsbHeartbeatMs.load(std::memory_order_acquire);
            return lastHeartbeat != 0U && HAL_GetTick() - lastHeartbeat < USB_COMPANION_LOSS_MS;
        }

        bool sendFrameToUsb(uint8_t type, uint8_t sequence, const uint8_t *payload, uint16_t payloadLength)
        {
            const size_t frameLength = encodeFrame(type, sequence, payload, payloadLength);
            return frameLength != 0U && USBCOM::write(txBuffer.data(), frameLength);
        }

        bool sendFrameTo(Link link, uint8_t type, uint8_t sequence, const uint8_t *payload, uint16_t payloadLength)
        {
            return link == Link::Uart ? sendFrameToUart(type, sequence, payload, payloadLength) : sendFrameToUsb(type, sequence, payload, payloadLength);
        }

        void broadcastFrame(uint8_t type, uint8_t sequence, const uint8_t *payload, uint16_t payloadLength)
        {
            (void)sendFrameToUart(type, sequence, payload, payloadLength);
            if (usbCompanionAlive()) (void)sendFrameToUsb(type, sequence, payload, payloadLength);
        }

        uint16_t makeStatusPayload(uint8_t *payload)
        {
            const FaultManager::Snapshot faultSnapshot = FaultManager::getSnapshot();
            const SlaveController::MeasurementSnapshot measurement = SlaveController::getMeasurementSnapshot();
            const bool measurementsFresh = SlaveController::areMeasurementsFresh();
            uint16_t flags = 0U;
            if (SlaveController::isHVReady()) flags |= 1U << 0U;
            if (SlaveController::isChargingAllowed()) flags |= 1U << 1U;
            if (PCC::isRunRequested()) flags |= 1U << 2U;
            if (measurementsFresh) flags |= 1U << 3U;
            if (hasValidGatewayFrame.load(std::memory_order_acquire) &&
                HAL_GetTick() - lastValidGatewayFrameMs.load(std::memory_order_acquire) < GATEWAY_LOSS_MS) flags |= 1U << 4U;
            if (SlaveController::isBalancingEnabled()) flags |= 1U << 5U;
            if (measurement.socValid) flags |= 1U << 6U;
            if (measurement.currentSensingEnabled) flags |= 1U << 7U;
            uint32_t socCalibrationUnixTime = 0U;
            if (SlaveController::getLastSoCCalibrationUnixTime(socCalibrationUnixTime)) flags |= 1U << 8U;

            payload[0] = static_cast<uint8_t>(faultSnapshot.bmsState);
            payload[1] = static_cast<uint8_t>(PCC::getPCCState());
            writeLe16(payload + 2U, flags);
            payload[4] = static_cast<uint8_t>(SlaveController::getNumOfSlaves());
            writeLe32(payload + 5U, faultSnapshot.bmsActive);
            writeLe32(payload + 9U, faultSnapshot.bmsLatched);
            writeLe32(payload + 13U, faultSnapshot.hvActive);
            writeLe32(payload + 17U, faultSnapshot.hvLatched);
            writeLe32(payload + 21U, faultSnapshot.warnings);
            writeLe32(payload + 25U, HAL_GetTick());
            writeLe32(payload + 29U, socCalibrationUnixTime);
            return 33U;
        }

        void sendStatus()
        {
            std::array<uint8_t, 33U> payload = {};
            broadcastFrame(STATUS, 0U, payload.data(), makeStatusPayload(payload.data()));
        }

        void sendPack(const SlaveController::MeasurementSnapshot &measurement)
        {
            std::array<uint8_t, 24U> payload = {};
            writeLe32(payload.data(), measurement.packVoltageUv);
            writeLe16(payload.data() + 4U, static_cast<uint16_t>(static_cast<int16_t>(BCC_CURRENT_TO_RAW(measurement.packCurrentA))));
            writeLe16(payload.data() + 6U, measurement.socRaw);
            writeLe32(payload.data() + 8U, measurement.minCellVoltageUv);
            writeLe32(payload.data() + 12U, measurement.maxCellVoltageUv);
            writeLe16(payload.data() + 16U, measurement.minNtcTemperatureRaw);
            writeLe16(payload.data() + 18U, measurement.maxNtcTemperatureRaw);
            writeLe16(payload.data() + 20U, measurement.minIcTemperatureRaw);
            writeLe16(payload.data() + 22U, measurement.maxIcTemperatureRaw);
            broadcastFrame(PACK, 0U, payload.data(), payload.size());
        }

        uint32_t voltageToMicrovolts(double voltage)
        {
            if (!std::isfinite(voltage) || voltage <= 0.0)
            {
                return 0U;
            }

            const double microvolts = voltage * 1'000'000.0;
            if (microvolts >= static_cast<double>(std::numeric_limits<uint32_t>::max()))
            {
                return std::numeric_limits<uint32_t>::max();
            }
            return static_cast<uint32_t>(microvolts + 0.5);
        }

        void sendHvVoltages()
        {
            const IO::HVVoltages voltages = IO::getHVVoltages();
            std::array<uint8_t, 12U> payload = {};
            if (voltages.valid)
            {
                payload[0] = 1U;
                writeLe32(payload.data() + 4U, voltageToMicrovolts(voltages.batteryVoltage));
                writeLe32(payload.data() + 8U, voltageToMicrovolts(voltages.loadVoltage));
            }
            broadcastFrame(HV_VOLTAGES, 0U, payload.data(), payload.size());
        }

        void sendCellsAndTemperatures(const SlaveController::MeasurementSnapshot &measurement)
        {
            const auto &allCells = measurement.cellVoltages;
            const auto &allNtc = measurement.ntcTemperatures;
            const auto &allIc = measurement.icTemperatures;
            const size_t slaveCount = SlaveController::getNumOfSlaves();

            // A bench chain may contain one monitor; emit exactly the configured count.
            for (size_t slaveIndex = 0U; slaveIndex < slaveCount; ++slaveIndex)
            {
                if (slaveIndex >= allCells.size() || slaveIndex >= allNtc.size() || slaveIndex >= allIc.size() ||
                    allCells[slaveIndex].size() < 12U || allNtc[slaveIndex].size() < 4U)
                {
                    return;
                }

                std::array<uint8_t, 51U> cellPayload = {};
                cellPayload[0] = static_cast<uint8_t>(slaveIndex);
                writeLe16(cellPayload.data() + 1U, slaveIndex < measurement.balancingMasks.size()
                                                        ? measurement.balancingMasks[slaveIndex]
                                                        : 0U);
                for (size_t cellIndex = 0U; cellIndex < 12U; ++cellIndex)
                {
                    writeLe32(cellPayload.data() + 3U + cellIndex * 4U, allCells[slaveIndex][cellIndex]);
                }
                broadcastFrame(CELL, 0U, cellPayload.data(), cellPayload.size());

                std::array<uint8_t, 11U> temperaturePayload = {};
                temperaturePayload[0] = static_cast<uint8_t>(slaveIndex);
                for (size_t ntcIndex = 0U; ntcIndex < 4U; ++ntcIndex)
                {
                    writeLe16(temperaturePayload.data() + 1U + ntcIndex * 2U, allNtc[slaveIndex][ntcIndex]);
                }
                writeLe16(temperaturePayload.data() + 9U, allIc[slaveIndex]);
                broadcastFrame(TEMPERATURE, 0U, temperaturePayload.data(), temperaturePayload.size());
            }
        }

        void sendEvent(uint8_t eventId, uint32_t value)
        {
            std::array<uint8_t, 5U> payload = {eventId, 0U, 0U, 0U, 0U};
            writeLe32(payload.data() + 1U, value);
            broadcastFrame(EVENT, 0U, payload.data(), payload.size());
        }

        void publishChangedEvents()
        {
            const FaultManager::Snapshot faultSnapshot = FaultManager::getSnapshot();
            const uint8_t bmsState = static_cast<uint8_t>(faultSnapshot.bmsState);
            const uint8_t hvState = static_cast<uint8_t>(PCC::getPCCState());
            const uint32_t bmsActive = faultSnapshot.bmsActive;
            const uint32_t bmsLatched = faultSnapshot.bmsLatched;
            const uint32_t hvActive = faultSnapshot.hvActive;
            const uint32_t hvLatched = faultSnapshot.hvLatched;
            const uint32_t warnings = faultSnapshot.warnings;
            const bool measurementsFresh = SlaveController::areMeasurementsFresh();

            if (!eventCache.initialized)
            {
                eventCache = {true, bmsState, hvState, bmsActive, bmsLatched, hvActive, hvLatched, warnings, measurementsFresh};
                return;
            }

            if (eventCache.bmsState != bmsState) sendEvent(0x01U, bmsState);
            if (eventCache.hvState != hvState) sendEvent(0x02U, hvState);
            if (eventCache.bmsActive != bmsActive) sendEvent(0x03U, bmsActive);
            if (eventCache.bmsLatched != bmsLatched) sendEvent(0x04U, bmsLatched);
            if (eventCache.hvActive != hvActive) sendEvent(0x05U, hvActive);
            if (eventCache.hvLatched != hvLatched) sendEvent(0x06U, hvLatched);
            if (eventCache.warnings != warnings) sendEvent(0x07U, warnings);
            if (eventCache.measurementsFresh != measurementsFresh) sendEvent(0x08U, measurementsFresh ? 1U : 0U);
            eventCache = {true, bmsState, hvState, bmsActive, bmsLatched, hvActive, hvLatched, warnings, measurementsFresh};
        }

        bool sendServiceResponse(Link link, uint8_t sequence, uint8_t serviceId, ServiceResult result,
                                 const uint8_t *data = nullptr, uint16_t dataLength = 0U)
        {
            std::array<uint8_t, 20U> payload = {};
            if (dataLength > payload.size() - 2U || (dataLength != 0U && data == nullptr)) return false;
            payload[0] = serviceId;
            payload[1] = static_cast<uint8_t>(result);
            if (dataLength != 0U)
            {
                std::memcpy(payload.data() + 2U, data, dataLength);
            }
            return sendFrameTo(link, SERVICE_RESPONSE, sequence, payload.data(), static_cast<uint16_t>(2U + dataLength));
        }

        bool sendServiceResponse(uint8_t sequence, uint8_t serviceId, ServiceResult result,
                                 const uint8_t *data = nullptr, uint16_t dataLength = 0U)
        {
            return sendServiceResponse(currentResponseLink, sequence, serviceId, result, data, dataLength);
        }

        uint8_t configurationReason(RuntimeConfiguration::LoadStatus status)
        {
            switch (status)
            {
            case RuntimeConfiguration::LoadStatus::Valid: return 0U;
            case RuntimeConfiguration::LoadStatus::Blank: return 1U;
            case RuntimeConfiguration::LoadStatus::VersionMismatch: return 2U;
            case RuntimeConfiguration::LoadStatus::Corrupt: return 3U;
            }
            return 3U;
        }

        void writeConfigurationResponse(uint8_t *data, const RuntimeConfiguration::LoadResult &result)
        {
            const RuntimeConfiguration::Values &values = result.status == RuntimeConfiguration::LoadStatus::Valid
                                                              ? result.values
                                                              : RuntimeConfiguration::defaults();
            data[0] = configurationReason(result.status);
            writeLe16(data + 1U, RuntimeConfiguration::CONFIG_VERSION);
            writeLe16(data + 3U, result.storedVersion);
            data[5] = values.slaveCount;
            data[6] = values.currentSenseSlave;
            writeLe32(data + 7U, values.shuntResistanceMicroOhms);
            writeLe32(data + 11U, values.batteryCapacityMilliAh);
            data[15] = values.invertCurrent ? 1U : 0U;
            data[16] = values.balanceEnabled ? 1U : 0U;
            data[17] = values.startupDiagnostics ? 1U : 0U;
        }

        bool configurationWriteAllowed()
        {
            const FaultManager::Snapshot faults = FaultManager::getSnapshot();
            return PCC::getPCCState() == PCC::OFF &&
                   !PCC::isFirmwareUpdatePrepared() &&
                   !PCC::isFirmwareUpdateLocked() &&
                   (!PCC::isRunRequested() || faults.bmsState == FaultManager::BmsState::Critical);
        }

        [[noreturn]] void enterSystemBootloader()
        {
            (void)HAL_UART_Abort(&huart1);

            // Keep the HAL timebase alive while HAL_RCC_DeInit() performs its
            // bounded clock transitions, but prevent FreeRTOS from switching
            // to another task after the system clock starts changing.
            vTaskSuspendAll();
            (void)HAL_RCC_DeInit();
            HAL_SuspendTick();
            __disable_irq();

            SysTick->CTRL = 0U;
            SysTick->LOAD = 0U;
            SysTick->VAL = 0U;

            // Reset every peripheral bus, then leave only the flash interface
            // clocked while this function is still executing from application
            // flash.  SYSCFG is enabled temporarily below for the required
            // system-memory remap.
            (void)HAL_DeInit();
            RCC->AHB1ENR = RCC_AHB1ENR_FLASHEN;
            RCC->AHB2ENR = 0U;
            RCC->AHB3ENR = 0U;
            RCC->APB1ENR1 = 0U;
            RCC->APB1ENR2 = 0U;
            RCC->APB2ENR = RCC_APB2ENR_SYSCFGEN;
            __DSB();

            // This is the final interrupt cleanup after every HAL call.  The
            // NVIC registers cover external interrupts; PendSV and SysTick are
            // system exceptions and must be cleared through SCB->ICSR.
            for (uint32_t index = 0U; index < 8U; ++index)
            {
                NVIC->ICER[index] = 0xFFFFFFFFUL;
                NVIC->ICPR[index] = 0xFFFFFFFFUL;
            }
            SCB->ICSR = SCB_ICSR_PENDSVCLR_Msk | SCB_ICSR_PENDSTCLR_Msk;

            // The STM32G491 system bootloader requires system flash to be
            // visible at address zero when entered through a software jump.
            __HAL_SYSCFG_REMAPMEMORY_SYSTEMFLASH();
            __DSB();
            RCC->APB2ENR = 0U;
            SCB->VTOR = SYSTEM_MEMORY_BASE;
            __DSB();
            __ISB();
            const uint32_t systemMsp = *reinterpret_cast<const uint32_t *>(SYSTEM_MEMORY_BASE);
            const uint32_t systemResetHandler = *reinterpret_cast<const uint32_t *>(SYSTEM_MEMORY_BASE + 4U);
            __set_MSP(systemMsp);

            // This runs from a FreeRTOS task, which normally uses PSP with
            // interrupt masking managed by the kernel.  The system-memory ROM
            // starts as if it has just been reset: privileged thread mode on
            // MSP, with exceptions unmasked.  Recreate that context before
            // calling its reset handler.  SysTick and every NVIC line remain
            // disabled above, so no application interrupt can run here.
            __set_CONTROL(0U);
            __set_BASEPRI(0U);
            __set_FAULTMASK(0U);
            __DSB();
            __ISB();
            __enable_irq();
            reinterpret_cast<void (*)(void)>(systemResetHandler)();
            while (true) {}
        }

        void processPendingRegisterRead()
        {
            if (!pendingRegisterRead.active)
            {
                return;
            }

            SlaveController::RegisterReponse response{};
            if (SlaveController::takeRegisterResponse(response))
            {
                if (response.status == BCC_STATUS_SUCCESS)
                {
                    std::array<uint8_t, 4U> payload = {
                        pendingRegisterRead.slaveIndex,
                        pendingRegisterRead.regAddr,
                        0U,
                        0U,
                    };
                    writeLe16(payload.data() + 2U, response.regValue);
                    sendServiceResponse(pendingRegisterRead.link, pendingRegisterRead.sequence, READ_REGISTER, OK, payload.data(), payload.size());
                }
                else
                {
                    sendServiceResponse(pendingRegisterRead.link, pendingRegisterRead.sequence, READ_REGISTER, DENIED);
                }
                pendingRegisterRead.active = false;
                return;
            }

            if (static_cast<int32_t>(HAL_GetTick() - pendingRegisterRead.deadlineMs) >= 0)
            {
                sendServiceResponse(pendingRegisterRead.link, pendingRegisterRead.sequence, READ_REGISTER, DENIED);
                pendingRegisterRead.active = false;
            }
        }

        void processServiceRequest(const ReceivedFrame &frame)
        {
            if (frame.sequence == 0U || frame.length == 0U)
            {
                return;
            }

            const uint8_t serviceId = frame.payload[0];
            currentResponseLink = frame.link;
            if (frame.link == Link::Usb && !usbCompanionAlive())
            {
                return;
            }
            if (PCC::isFirmwareUpdateLocked())
            {
                sendServiceResponse(frame.sequence, serviceId, DENIED);
                return;
            }
            if (PCC::isFirmwareUpdatePrepared() && serviceId != COMMIT_STM32_BOOTLOADER)
            {
                sendServiceResponse(frame.sequence, serviceId, BUSY);
                return;
            }
            if (pendingRegisterRead.active)
            {
                sendServiceResponse(frame.sequence, serviceId, BUSY);
                return;
            }

            switch (serviceId)
            {
            case GET_STATUS:
            {
                if (frame.length != 1U)
                {
                    sendServiceResponse(frame.sequence, serviceId, INVALID);
                    return;
                }
                std::array<uint8_t, 33U> status = {};
                sendServiceResponse(frame.link, frame.sequence, serviceId, OK, status.data(), makeStatusPayload(status.data()));
                return;
            }

            case SET_RUN_REQUEST:
                if (frame.length != 2U || frame.payload[1] > 1U)
                {
                    sendServiceResponse(frame.sequence, serviceId, INVALID);
                    return;
                }
                PCC::setRunRequest(frame.payload[1] != 0U);
                sendServiceResponse(frame.sequence, serviceId, OK);
                return;

            case ACKNOWLEDGE_FAULTS:
                if (frame.length != 1U)
                {
                    sendServiceResponse(frame.sequence, serviceId, INVALID);
                    return;
                }
                if (!FaultManager::acknowledge())
                {
                    sendServiceResponse(frame.sequence, serviceId, DENIED);
                    return;
                }
                sendServiceResponse(frame.sequence, serviceId, OK);
                return;

            case READ_REGISTER:
                if (frame.length != 3U || frame.payload[1] >= SlaveController::getNumOfSlaves())
                {
                    sendServiceResponse(frame.sequence, serviceId, INVALID);
                    return;
                }
                pendingRegisterRead = {};
                pendingRegisterRead.link = frame.link;
                pendingRegisterRead.sequence = frame.sequence;
                pendingRegisterRead.slaveIndex = frame.payload[1];
                pendingRegisterRead.regAddr = frame.payload[2];
                pendingRegisterRead.deadlineMs = HAL_GetTick() + REGISTER_READ_TIMEOUT_MS;
                if (!SlaveController::requestRegister(
                        {.cid = static_cast<uint8_t>(pendingRegisterRead.slaveIndex + 1U), .regAddr = pendingRegisterRead.regAddr}))
                {
                    sendServiceResponse(frame.sequence, serviceId, DENIED);
                    return;
                }
                pendingRegisterRead.active = true;
                return;

            case SET_RTC:
                if (frame.length != 5U)
                {
                    sendServiceResponse(frame.sequence, serviceId, INVALID);
                    return;
                }
                if (!RtcTime::isSupportedUnixTime(readLe32(frame.payload.data() + 1U)))
                {
                    sendServiceResponse(frame.sequence, serviceId, INVALID);
                    return;
                }
                if (!RtcTime::setUnixTime(readLe32(frame.payload.data() + 1U)))
                {
                    sendServiceResponse(frame.sequence, serviceId, DENIED);
                    return;
                }
                sendServiceResponse(frame.sequence, serviceId, OK);
                return;

            case SET_BALANCING_ENABLED:
                if (frame.length != 2U || frame.payload[1] > 1U)
                {
                    sendServiceResponse(frame.sequence, serviceId, INVALID);
                    return;
                }
                if (!RuntimeConfiguration::updateBalanceEnabled(frame.payload[1] != 0U))
                {
                    sendServiceResponse(frame.sequence, serviceId, DENIED);
                    return;
                }
                SlaveController::setBalancingEnabled(frame.payload[1] != 0U);
                sendServiceResponse(frame.sequence, serviceId, OK);
                return;

            case GET_RTC:
            {
                if (frame.length != 1U)
                {
                    sendServiceResponse(frame.sequence, serviceId, INVALID);
                    return;
                }
                uint32_t unixTime = 0U;
                if (!RtcTime::getUnixTime(unixTime))
                {
                    sendServiceResponse(frame.sequence, serviceId, DENIED);
                    return;
                }
                std::array<uint8_t, 4U> response = {};
                writeLe32(response.data(), unixTime);
                sendServiceResponse(frame.sequence, serviceId, OK, response.data(), response.size());
                return;
            }

            case GET_DEVICE_INFO:
            {
                if (frame.length != 1U)
                {
                    sendServiceResponse(frame.sequence, serviceId, INVALID);
                    return;
                }
                std::array<uint8_t, 4U> version = {};
                writeLe32(version.data(), FIRMWARE_VERSION_PACKED);
                sendServiceResponse(frame.sequence, serviceId, OK, version.data(), version.size());
                return;
            }

            case PREPARE_STM32_BOOTLOADER:
            {
                if (frame.length != 13U)
                {
                    sendServiceResponse(frame.sequence, serviceId, INVALID);
                    return;
                }
                if (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED)
                {
                    // The STM32 ROM bootloader gives an enumerated USB host
                    // precedence over USART1.  USB power without a host does
                    // not configure this device and remains supported.
                    sendServiceResponse(frame.sequence, serviceId, USB_HOST_ACTIVE);
                    return;
                }
                const uint32_t imageLength = readLe32(frame.payload.data() + 5U);
                if (imageLength < 8U || imageLength > APP_FLASH_BYTES || !PCC::prepareFirmwareUpdate())
                {
                    sendServiceResponse(frame.sequence, serviceId, DENIED);
                    return;
                }
                // PREPARE is reversible. The Gateway must explicitly COMMIT
                // after it receives this response; otherwise PCC expires the
                // prepared safe-off state and returns to normal operation.
                sendServiceResponse(frame.sequence, serviceId, OK);
                return;
            }

            case GET_CONFIG:
            {
                if (frame.length != 1U)
                {
                    sendServiceResponse(frame.sequence, serviceId, INVALID);
                    return;
                }
                const RuntimeConfiguration::LoadResult configuration = RuntimeConfiguration::load();
                std::array<uint8_t, 18U> response = {};
                writeConfigurationResponse(response.data(), configuration);
                sendServiceResponse(frame.link, frame.sequence, serviceId, OK, response.data(), response.size());
                return;
            }

            case GET_DIAGNOSTIC_REPORT:
            {
                if (frame.length != 2U || frame.payload[1] >= SlaveController::getNumOfSlaves())
                {
                    sendServiceResponse(frame.sequence, serviceId, INVALID);
                    return;
                }
                SlaveController::DiagnosticReport report{};
                if (!SlaveController::getDiagnosticReport(frame.payload[1], report))
                {
                    sendServiceResponse(frame.sequence, serviceId, DENIED);
                    return;
                }
                std::array<uint8_t, 6U> response = {
                    frame.payload[1],
                    report.cid,
                    static_cast<uint8_t>(report.failedChecks),
                    static_cast<uint8_t>(report.failedChecks >> 8U),
                    report.status,
                    report.failedDiagnostic,
                };
                sendServiceResponse(frame.link, frame.sequence, serviceId, OK, response.data(), response.size());
                return;
            }

            case SET_CONFIG:
            {
                if (frame.length != 14U || frame.payload[11] > 1U || frame.payload[12] > 1U || frame.payload[13] > 1U)
                {
                    sendServiceResponse(frame.sequence, serviceId, INVALID);
                    return;
                }
                RuntimeConfiguration::Values values = {
                    .slaveCount = frame.payload[1],
                    .currentSenseSlave = frame.payload[2],
                    .shuntResistanceMicroOhms = readLe32(frame.payload.data() + 3U),
                    .batteryCapacityMilliAh = readLe32(frame.payload.data() + 7U),
                    .invertCurrent = frame.payload[11] != 0U,
                    .balanceEnabled = frame.payload[12] != 0U,
                    .startupDiagnostics = frame.payload[13] != 0U,
                };
                if (!SlaveController::validateRuntimeConfiguration(values))
                {
                    sendServiceResponse(frame.sequence, serviceId, INVALID);
                    return;
                }
                if (!configurationWriteAllowed())
                {
                    sendServiceResponse(frame.sequence, serviceId, DENIED);
                    return;
                }
                if (!RuntimeConfiguration::save(values))
                {
                    sendServiceResponse(frame.sequence, serviceId, DENIED);
                    return;
                }
                (void)sendServiceResponse(frame.sequence, serviceId, OK);
                // USB writes are queued for the CDC transmit task. Give that
                // task time to drain the success response before resetting;
                // the UART path is already blocking until transmission ends.
                osDelay(100U);
                NVIC_SystemReset();
                return;
            }

            case COMMIT_STM32_BOOTLOADER:
            {
                if (frame.length != 1U)
                {
                    sendServiceResponse(frame.sequence, serviceId, INVALID);
                    return;
                }
                if (!PCC::commitFirmwareUpdate())
                {
                    sendServiceResponse(frame.sequence, serviceId, DENIED);
                    return;
                }
                // This final request deliberately has no service response.
                // The Gateway treats successful transmission as the commit and
                // immediately changes USART1 to the STM32 ROM protocol.
                enterSystemBootloader();
                return;
            }

            default:
                sendServiceResponse(frame.sequence, serviceId, INVALID);
                return;
            }
        }

        void processReceivedFrame(const ReceivedFrame &frame)
        {
            if (frame.type == HEARTBEAT)
            {
                if (frame.link == Link::Usb && frame.sequence == 0U && frame.length == 0U)
                {
                    lastValidUsbHeartbeatMs.store(HAL_GetTick(), std::memory_order_release);
                }
                return;
            }
            if (frame.type == SERVICE_REQUEST)
            {
                processServiceRequest(frame);
            }
        }

        void task(void *)
        {
            (void)armReceiveDma();
            uint32_t lastHeartbeatMs = HAL_GetTick();
            uint32_t lastStatusMs = HAL_GetTick();
            uint32_t lastSnapshotMs = 0U;

            while (true)
            {
                ReceivedFrame frame = {};
                while (xQueueReceive(rxQueue, &frame, 0U) == pdPASS)
                {
                    processReceivedFrame(frame);
                }

                processPendingRegisterRead();
                publishChangedEvents();

                const uint32_t now = HAL_GetTick();
                processReceiveRecovery(now);
                if (now - lastHeartbeatMs >= HEARTBEAT_PERIOD_MS)
                {
                    broadcastFrame(HEARTBEAT, 0U, nullptr, 0U);
                    lastHeartbeatMs = now;
                }
                if (now - lastStatusMs >= HEARTBEAT_PERIOD_MS)
                {
                    sendStatus();
                    lastStatusMs = now;
                }
                if (now - lastSnapshotMs >= SNAPSHOT_PERIOD_MS &&
                    SlaveController::isNewDataAvailable(lastSeenMeasurement))
                {
                    sendStatus();
                    sendHvVoltages();
                    const SlaveController::MeasurementSnapshot measurement = SlaveController::getMeasurementSnapshot();
                    sendPack(measurement);
                    sendCellsAndTemperatures(measurement);
                    lastSnapshotMs = now;
                }
                osDelay(10U);
            }
        }
    }

    void setup()
    {
        hasValidGatewayFrame.store(false, std::memory_order_release);
        lastValidGatewayFrameMs.store(HAL_GetTick(), std::memory_order_release);
        uartStartedMs.store(lastValidGatewayFrameMs.load(std::memory_order_acquire), std::memory_order_release);
        lastValidUsbHeartbeatMs.store(0U, std::memory_order_release);
        rxRecoveryRequested.store(false, std::memory_order_release);
        rxErrorCount.store(0U, std::memory_order_relaxed);
        lastRxErrorCode.store(HAL_UART_ERROR_NONE, std::memory_order_relaxed);
        rxArmFailureCount.store(0U, std::memory_order_relaxed);
        rxRecoveryCount.store(0U, std::memory_order_relaxed);
        nextRxRecoveryAttemptMs = 0U;
        resetUsbCandidate();
        rxQueue = xQueueCreateStatic(RX_QUEUE_DEPTH, sizeof(ReceivedFrame), rxQueueStorage.data(), &rxQueueControl);
        uartTaskHandle = osThreadNew(task, nullptr, &uartTaskAttributes);
    }

    bool isGatewayLinkLost()
    {
        return gatewayLinkLostAt(HAL_GetTick());
    }

    void onRxEvent(const uint8_t *data, size_t length)
    {
        for (size_t index = 0U; index < length; ++index)
        {
            parseByteFromIsr(data[index]);
        }
    }

    void onUsbRxData(const uint8_t *data, size_t length)
    {
        if (data == nullptr) return;
        for (size_t index = 0U; index < length; ++index)
        {
            consumeUsbByteFromIsr(data[index]);
        }
    }

    void onTxComplete()
    {
        txComplete.store(true, std::memory_order_release);
    }

    void onError()
    {
        rxErrorCount.fetch_add(1U, std::memory_order_relaxed);
        lastRxErrorCode.store(HAL_UART_GetError(&huart1), std::memory_order_relaxed);
        resetParser();
        rxRecoveryRequested.store(true, std::memory_order_release);
    }

    void onHalRxEvent(uint16_t size)
    {
        onRxEvent(rxDmaBuffer.data(), size);
        (void)armReceiveDma();
    }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
    if (huart->Instance == USART1)
    {
        BmsUart::onHalRxEvent(size);
    }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        BmsUart::onTxComplete();
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        BmsUart::onError();
    }
}

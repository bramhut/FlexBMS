#include "BmsUart.h"

#include "FirmwareVersion.h"
#include "FreeRTOS.h"
#include "TimeFunctions.h"
#include "cmsis_os.h"
#include "bcc/SlaveController.h"
#include "bcc/bcc_utils.h"
#include "main.h"
#include "pcc.h"
#include "queue.h"
#include "usart.h"
#include "USBCOM.h"

#include <array>
#include <cstring>

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
        constexpr uint32_t REGISTER_READ_TIMEOUT_MS = 500U;
        constexpr uint32_t TX_TIMEOUT_MS = 50U;
        constexpr uint32_t APP_FLASH_BYTES = 512U * 1024U;
        constexpr uintptr_t SYSTEM_MEMORY_BASE = 0x1FFF0000UL;

        enum MessageType : uint8_t
        {
            HEARTBEAT = 0x01U,
            STATUS = 0x02U,
            PACK = 0x03U,
            CELL = 0x04U,
            TEMPERATURE = 0x05U,
            SERVICE_REQUEST = 0x10U,
            SERVICE_RESPONSE = 0x11U,
            EVENT = 0x12U,
        };

        enum ServiceId : uint8_t
        {
            GET_STATUS = 0x01U,
            SET_RUN_REQUEST = 0x02U,
            CLEAR_FAULTS = 0x03U,
            READ_REGISTER = 0x04U,
            SET_RTC = 0x05U,
            GET_DEVICE_INFO = 0x06U,
            ENTER_STM32_BOOTLOADER = 0x07U,
            GET_RTC = 0x08U,
        };

        enum ServiceResult : uint8_t
        {
            OK = 0U,
            DENIED = 1U,
            INVALID = 2U,
            BUSY = 3U,
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
            bool finished = false;
            uint8_t sequence = 0U;
            uint8_t slaveIndex = 0U;
            uint8_t regAddr = 0U;
            uint32_t deadlineMs = 0U;
            SlaveController::RegisterReponse response = {};
        };

        struct EventCache
        {
            bool initialized = false;
            uint8_t bmsState = 0U;
            uint8_t hvState = 0U;
            uint16_t bmsActive = 0U;
            uint16_t bmsLatched = 0U;
            uint16_t hvActive = 0U;
            uint16_t hvLatched = 0U;
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
        volatile bool txComplete = false;
        volatile bool hasValidGatewayFrame = false;
        volatile uint32_t lastValidGatewayFrameMs = 0U;
        volatile uint32_t uartStartedMs = 0U;
        volatile uint32_t lastValidUsbHeartbeatMs = 0U;

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

        void armReceiveDma()
        {
            if (HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rxDmaBuffer.data(), rxDmaBuffer.size()) == HAL_OK)
            {
                __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT);
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
                hasValidGatewayFrame = true;
                lastValidGatewayFrameMs = HAL_GetTick();
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
            txComplete = false;
            if (HAL_UART_Transmit_DMA(&huart1, txBuffer.data(), frameLength) != HAL_OK)
            {
                return false;
            }

            const uint32_t deadline = HAL_GetTick() + TX_TIMEOUT_MS;
            while (!txComplete)
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
            const uint32_t lastHeartbeat = lastValidUsbHeartbeatMs;
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
            const bool measurementsFresh = SlaveController::areMeasurementsFresh();
            uint16_t flags = 0U;
            if (SlaveController::isHVReady()) flags |= 1U << 0U;
            if (SlaveController::isChargingAllowed()) flags |= 1U << 1U;
            if (PCC::isRunRequested()) flags |= 1U << 2U;
            if (measurementsFresh) flags |= 1U << 3U;
            if (hasValidGatewayFrame && HAL_GetTick() - lastValidGatewayFrameMs < GATEWAY_LOSS_MS) flags |= 1U << 4U;

            payload[0] = static_cast<uint8_t>(SlaveController::getState());
            payload[1] = static_cast<uint8_t>(PCC::getPCCState());
            writeLe16(payload + 2U, flags);
            payload[4] = static_cast<uint8_t>(SlaveController::getNumOfSlaves());
            writeLe16(payload + 5U, SlaveController::getActiveFaults());
            writeLe16(payload + 7U, SlaveController::getLatchedFaults());
            writeLe16(payload + 9U, PCC::getActiveErrors());
            writeLe16(payload + 11U, PCC::getLatchedErrors());
            writeLe32(payload + 13U, HAL_GetTick());
            return 17U;
        }

        void sendStatus()
        {
            std::array<uint8_t, 17U> payload = {};
            broadcastFrame(STATUS, 0U, payload.data(), makeStatusPayload(payload.data()));
        }

        void sendPack()
        {
            std::array<uint8_t, 24U> payload = {};
            writeLe32(payload.data(), SlaveController::getPackVoltage());
            writeLe16(payload.data() + 4U, static_cast<uint16_t>(static_cast<int16_t>(BCC_CURRENT_TO_RAW(SlaveController::getCurrent()))));
            writeLe16(payload.data() + 6U, SlaveController::getSoC());
            writeLe32(payload.data() + 8U, SlaveController::getMinCellVoltage());
            writeLe32(payload.data() + 12U, SlaveController::getMaxCellVoltage());
            writeLe16(payload.data() + 16U, SlaveController::getMinNTCtemp());
            writeLe16(payload.data() + 18U, SlaveController::getMaxNTCtemp());
            writeLe16(payload.data() + 20U, SlaveController::getMinICtemp());
            writeLe16(payload.data() + 22U, SlaveController::getMaxICtemp());
            broadcastFrame(PACK, 0U, payload.data(), payload.size());
        }

        void sendCellsAndTemperatures()
        {
            const auto &allCells = SlaveController::getCellVoltages();
            const auto &allNtc = SlaveController::getNTCtemps();
            const auto &allIc = SlaveController::getICtemps();
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
                writeLe16(cellPayload.data() + 1U, SlaveController::getBalancingMask(slaveIndex));
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

        void sendEvent(uint8_t eventId, uint16_t value)
        {
            std::array<uint8_t, 3U> payload = {eventId, 0U, 0U};
            writeLe16(payload.data() + 1U, value);
            broadcastFrame(EVENT, 0U, payload.data(), payload.size());
        }

        void publishChangedEvents()
        {
            const uint8_t bmsState = static_cast<uint8_t>(SlaveController::getState());
            const uint8_t hvState = static_cast<uint8_t>(PCC::getPCCState());
            const uint16_t bmsActive = SlaveController::getActiveFaults();
            const uint16_t bmsLatched = SlaveController::getLatchedFaults();
            const uint16_t hvActive = PCC::getActiveErrors();
            const uint16_t hvLatched = PCC::getLatchedErrors();
            const bool measurementsFresh = SlaveController::areMeasurementsFresh();

            if (!eventCache.initialized)
            {
                eventCache = {true, bmsState, hvState, bmsActive, bmsLatched, hvActive, hvLatched, measurementsFresh};
                return;
            }

            if (eventCache.bmsState != bmsState) sendEvent(0x01U, bmsState);
            if (eventCache.hvState != hvState) sendEvent(0x02U, hvState);
            if (eventCache.bmsActive != bmsActive) sendEvent(0x03U, bmsActive);
            if (eventCache.bmsLatched != bmsLatched) sendEvent(0x04U, bmsLatched);
            if (eventCache.hvActive != hvActive) sendEvent(0x05U, hvActive);
            if (eventCache.hvLatched != hvLatched) sendEvent(0x06U, hvLatched);
            if (eventCache.measurementsFresh != measurementsFresh) sendEvent(0x07U, measurementsFresh ? 1U : 0U);
            eventCache = {true, bmsState, hvState, bmsActive, bmsLatched, hvActive, hvLatched, measurementsFresh};
        }

        bool sendServiceResponse(Link link, uint8_t sequence, uint8_t serviceId, ServiceResult result,
                                 const uint8_t *data = nullptr, uint16_t dataLength = 0U)
        {
            std::array<uint8_t, 19U> payload = {};
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

        [[noreturn]] void enterSystemBootloader()
        {
            (void)HAL_UART_Abort(&huart1);
            __disable_irq();
            SysTick->CTRL = 0U;
            for (uint32_t index = 0U; index < 8U; ++index)
            {
                NVIC->ICER[index] = 0xFFFFFFFFUL;
                NVIC->ICPR[index] = 0xFFFFFFFFUL;
            }

            HAL_RCC_DeInit();
            SCB->VTOR = SYSTEM_MEMORY_BASE;
            __DSB();
            __ISB();
            const uint32_t systemMsp = *reinterpret_cast<const uint32_t *>(SYSTEM_MEMORY_BASE);
            const uint32_t systemResetHandler = *reinterpret_cast<const uint32_t *>(SYSTEM_MEMORY_BASE + 4U);
            __set_MSP(systemMsp);
            __DSB();
            __ISB();
            reinterpret_cast<void (*)(void)>(systemResetHandler)();
            while (true) {}
        }

        void processPendingRegisterRead()
        {
            if (!pendingRegisterRead.active)
            {
                return;
            }

            if (pendingRegisterRead.finished)
            {
                if (pendingRegisterRead.response.status == BCC_STATUS_SUCCESS)
                {
                    std::array<uint8_t, 4U> payload = {
                        pendingRegisterRead.slaveIndex,
                        pendingRegisterRead.regAddr,
                        0U,
                        0U,
                    };
                    writeLe16(payload.data() + 2U, pendingRegisterRead.response.regValue);
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
                std::array<uint8_t, 17U> status = {};
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

            case CLEAR_FAULTS:
                if (frame.length != 1U)
                {
                    sendServiceResponse(frame.sequence, serviceId, INVALID);
                    return;
                }
                if (!PCC::requestFaultClear())
                {
                    sendServiceResponse(frame.sequence, serviceId, DENIED);
                    return;
                }
                SlaveController::clearFaults();
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
                        {.cid = static_cast<uint8_t>(pendingRegisterRead.slaveIndex + 1U), .regAddr = pendingRegisterRead.regAddr},
                        &pendingRegisterRead.finished, &pendingRegisterRead.response))
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
                setRTCtime(readLe32(frame.payload.data() + 1U));
                sendServiceResponse(frame.sequence, serviceId, OK);
                return;

            case GET_RTC:
            {
                if (frame.length != 1U)
                {
                    sendServiceResponse(frame.sequence, serviceId, INVALID);
                    return;
                }
                time_t unixTime = 0;
                if (!getRTCtimeUNIX(unixTime) || unixTime < 0 || static_cast<uint64_t>(unixTime) > UINT32_MAX)
                {
                    sendServiceResponse(frame.sequence, serviceId, DENIED);
                    return;
                }
                std::array<uint8_t, 4U> response = {};
                writeLe32(response.data(), static_cast<uint32_t>(unixTime));
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

            case ENTER_STM32_BOOTLOADER:
            {
                if (frame.length != 13U)
                {
                    sendServiceResponse(frame.sequence, serviceId, INVALID);
                    return;
                }
                const uint32_t imageLength = readLe32(frame.payload.data() + 5U);
                if (imageLength < 8U || imageLength > APP_FLASH_BYTES || !PCC::enterFirmwareUpdateLock())
                {
                    sendServiceResponse(frame.sequence, serviceId, DENIED);
                    return;
                }
                if (sendServiceResponse(frame.sequence, serviceId, OK))
                {
                    enterSystemBootloader();
                }
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
                    lastValidUsbHeartbeatMs = HAL_GetTick();
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
            armReceiveDma();
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
                    sendPack();
                    sendCellsAndTemperatures();
                    lastSnapshotMs = now;
                }
                osDelay(10U);
            }
        }
    }

    void setup()
    {
        hasValidGatewayFrame = false;
        lastValidGatewayFrameMs = HAL_GetTick();
        uartStartedMs = lastValidGatewayFrameMs;
        lastValidUsbHeartbeatMs = 0U;
        resetUsbCandidate();
        rxQueue = xQueueCreateStatic(RX_QUEUE_DEPTH, sizeof(ReceivedFrame), rxQueueStorage.data(), &rxQueueControl);
        uartTaskHandle = osThreadNew(task, nullptr, &uartTaskAttributes);
    }

    bool isGatewayLinkLost()
    {
        const uint32_t now = HAL_GetTick();
        return now - uartStartedMs >= GATEWAY_LOSS_MS &&
               (!hasValidGatewayFrame || now - lastValidGatewayFrameMs >= GATEWAY_LOSS_MS);
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
        txComplete = true;
    }

    void onError()
    {
        resetParser();
        armReceiveDma();
    }

    void onHalRxEvent(uint16_t size)
    {
        onRxEvent(rxDmaBuffer.data(), size);
        armReceiveDma();
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

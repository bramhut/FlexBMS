#include "Peripherals/GoodweCan.h"

#include "Peripherals/GoodweCanCodec.h"
#include "Peripherals/GoodweCanConfig.h"
#include "TimeFunctions.h"
#include "bcc/SlaveController.h"
#include "cmsis_os.h"
#include "fdcan.h"

#define DEBUG_LVL 2
#include "Debug.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace GoodweCan
{
    namespace
    {
        constexpr uint32_t RX_QUEUE_DEPTH = 16U;
        constexpr uint32_t TASK_PERIOD_MS = 5U;

        CAN *mCan = nullptr;
        osMessageQueueId_t rxQueue = nullptr;
        osThreadId_t taskHandle = nullptr;
        Diagnostics diagnostics{};

        const osThreadAttr_t taskAttributes = {
            .name = "goodweCan",
            .stack_size = 2048U,
            .priority = (osPriority_t)osPriorityNormal,
        };

        bool configureBusTiming()
        {
            if (HAL_FDCAN_DeInit(&hfdcan1) != HAL_OK)
            {
                PRINTF_ERR("[GW-CAN] FDCAN de-init failed\n");
                return false;
            }

            hfdcan1.Init.NominalPrescaler = GOODWE_CAN_NOMINAL_PRESCALER;
            hfdcan1.Init.NominalSyncJumpWidth = GOODWE_CAN_NOMINAL_SYNC_JUMP_WIDTH;
            hfdcan1.Init.NominalTimeSeg1 = GOODWE_CAN_NOMINAL_TIME_SEGMENT_1;
            hfdcan1.Init.NominalTimeSeg2 = GOODWE_CAN_NOMINAL_TIME_SEGMENT_2;

            if (HAL_FDCAN_Init(&hfdcan1) != HAL_OK)
            {
                PRINTF_ERR("[GW-CAN] FDCAN init failed for %lu bit/s\n",
                           static_cast<unsigned long>(GOODWE_CAN_BITRATE));
                return false;
            }

            PRINTF_INFO("[GW-CAN] Selected protocol %u at %lu bit/s\n",
                        static_cast<unsigned>(GOODWE_CAN_PROTOCOL),
                        static_cast<unsigned long>(GOODWE_CAN_BITRATE));
            return true;
        }

        FrameDiagnostics *diagnosticsForId(uint32_t id)
        {
            switch (id)
            {
            case 0x420U:
                return &diagnostics.timeout420;
            case 0x425U:
                return &diagnostics.inverter425;
            case 0x305U:
                return &diagnostics.inverter305;
            default:
                return nullptr;
            }
        }

        void recordReceivedFrame(const CAN::Frame &frame)
        {
            if (frame.isExtended || frame.isRtr)
            {
                return;
            }

            FrameDiagnostics *target = diagnosticsForId(frame.id);
            if (target == nullptr)
            {
                return;
            }

            taskENTER_CRITICAL();
            target->count++;
            target->lastSeenMs = millis();
            target->length = std::min<uint8_t>(frame.length, 8U);
            for (uint8_t index = 0U; index < target->length; ++index)
            {
                target->data[index] = frame.data[index];
            }
            taskEXIT_CRITICAL();
        }

        void drainReceivedFrames()
        {
            if (rxQueue == nullptr)
            {
                return;
            }

            CAN::Frame frame{};
            while (osMessageQueueGet(rxQueue, &frame, nullptr, 0U) == osOK)
            {
                recordReceivedFrame(frame);
            }
        }

        uint16_t toUnsignedDeci(double value)
        {
            if (!std::isfinite(value) || value <= 0.0)
            {
                return 0U;
            }
            return static_cast<uint16_t>(std::clamp(std::lround(value * 10.0), 0L, 65535L));
        }

        int16_t toSignedDeci(double value)
        {
            if (!std::isfinite(value))
            {
                return 0;
            }
            return static_cast<int16_t>(std::clamp(std::lround(value * 10.0), -32768L, 32767L));
        }

        bool makeCodecData(GoodweCan::CodecData &data)
        {
            const SlaveController::BatteryCanSnapshot snapshot =
                SlaveController::getBatteryCanSnapshot();
            if (!snapshot.valid ||
                !std::isfinite(snapshot.packCurrentA) ||
                !std::isfinite(snapshot.chargeVoltageV) ||
                !std::isfinite(snapshot.dischargeVoltageV) ||
                !std::isfinite(snapshot.chargeCurrentA) ||
                !std::isfinite(snapshot.dischargeCurrentA) ||
                !std::isfinite(snapshot.averageTemperatureC) ||
                snapshot.cellCount == 0U)
            {
                return false;
            }

            data.commonSafe = snapshot.commonSafe;
            data.chargeAllowed = snapshot.chargeAllowed;
            data.dischargeAllowed = snapshot.dischargeAllowed;
            data.cellOverVoltage = snapshot.cellOverVoltage;
            data.cellUnderVoltage = snapshot.cellUnderVoltage;
            data.overTemperature = snapshot.overTemperature;
            data.underTemperature = snapshot.underTemperature;
            data.overCurrent = snapshot.overCurrent;
            data.communicationFault = snapshot.communicationFault;
            data.internalFault = snapshot.internalFault;

            data.moduleCount = static_cast<uint16_t>(GOODWE_CAN_A_MODULE_COUNT);
            data.socPercent = snapshot.socPercent;
            data.sohPercent = static_cast<uint16_t>(GOODWE_CAN_SOH_PERCENT);
            data.chargeVoltageDeciV = toUnsignedDeci(snapshot.chargeVoltageV);
            data.dischargeVoltageDeciV = toUnsignedDeci(snapshot.dischargeVoltageV);
            data.chargeCurrentDeciA = toUnsignedDeci(snapshot.chargeCurrentA);
            data.dischargeCurrentDeciA = toUnsignedDeci(snapshot.dischargeCurrentA);
            data.packVoltageDeciV = static_cast<uint16_t>(std::clamp(
                std::lround(static_cast<double>(snapshot.packVoltageUv) / 100000.0), 0L, 65535L));
            data.packCurrentDeciA = toSignedDeci(snapshot.packCurrentA);
            data.averageTemperatureDeciC = toSignedDeci(snapshot.averageTemperatureC);
            return true;
        }

        bool sendEncodedFrame(const GoodweCan::EncodedFrame &encoded)
        {
            CAN::Frame frame{};
            frame.id = encoded.id;
            frame.isExtended = false;
            frame.isRtr = false;
            frame.length = encoded.length;
            for (uint8_t index = 0U; index < encoded.length && index < 8U; ++index)
            {
                frame.data[index] = encoded.data[index];
            }

            if (mCan->sendMessage(frame))
            {
                return true;
            }

            taskENTER_CRITICAL();
            diagnostics.transmitFailures++;
            taskEXIT_CRITICAL();
            return false;
        }

        void transmitCycle()
        {
            GoodweCan::CodecData data{};
            if (!makeCodecData(data))
            {
                return;
            }

#if GOODWE_CAN_PROTOCOL == GOODWE_CAN_PROTOCOL_A
            constexpr std::array<uint32_t, 7U> frameIds = {
                0x453U, 0x455U, 0x456U, 0x457U, 0x458U, 0x45AU, 0x460U};
#else
            constexpr std::array<uint32_t, 4U> frameIds = {
                0x351U, 0x355U, 0x356U, 0x359U};
#endif

            for (const uint32_t id : frameIds)
            {
                GoodweCan::EncodedFrame encoded{};
#if GOODWE_CAN_PROTOCOL == GOODWE_CAN_PROTOCOL_A
                if (!GoodweCan::encodeCandidateA(id, data, encoded))
                {
                    continue;
                }
#else
                if (!GoodweCan::encodeCandidateB(id, data, encoded))
                {
                    continue;
                }
#endif
                sendEncodedFrame(encoded);
                osDelay(2U);
            }
        }

        void task(void *)
        {
            uint32_t nextTransmit = osKernelGetTickCount();
            const uint32_t periodTicks = GOODWE_CAN_PERIOD_MS / portTICK_PERIOD_MS;

            while (true)
            {
                drainReceivedFrames();

                const uint32_t now = osKernelGetTickCount();
                if (static_cast<int32_t>(now - nextTransmit) >= 0)
                {
                    transmitCycle();
                    nextTransmit += periodTicks;
                }

                osDelay(TASK_PERIOD_MS);
            }
        }
    }

    bool setup(CAN *can)
    {
        if (can == nullptr || mCan != nullptr)
        {
            PRINTF_ERR("[GW-CAN] Invalid or duplicate setup\n");
            return false;
        }

        if (!configureBusTiming())
        {
            return false;
        }

        mCan = can;
        mCan->setup();

        rxQueue = osMessageQueueNew(RX_QUEUE_DEPTH, sizeof(CAN::Frame), nullptr);
        if (rxQueue == nullptr)
        {
            PRINTF_ERR("[GW-CAN] Cannot allocate RX queue\n");
            mCan = nullptr;
            return false;
        }

        if (!mCan->addListenerAll(rxQueue))
        {
            PRINTF_ERR("[GW-CAN] Cannot install RX listener\n");
            return false;
        }

        return true;
    }

    bool start()
    {
        if (mCan == nullptr || rxQueue == nullptr || taskHandle != nullptr)
        {
            return false;
        }

        taskHandle = osThreadNew(task, nullptr, &taskAttributes);
        return taskHandle != nullptr;
    }

    Diagnostics getDiagnostics()
    {
        Diagnostics copy{};
        taskENTER_CRITICAL();
        copy = diagnostics;
        taskEXIT_CRITICAL();
        return copy;
    }
}


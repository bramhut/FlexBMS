#include "pcc.h"

#include "BoardIO.h"
#include "TimeFunctions.h"
#include "bcc/SlaveController.h"
#include "bcc/UserSettings.h"

#include <algorithm>
#include <cmath>

#define DEBUG_LVL 2
#include "Debug.h"

namespace PCC
{
    namespace
    {
        constexpr uint32_t PRECHARGE_TIMEOUT_MS = 10000U;
        constexpr uint32_t PRECHARGE_STABLE_TIME_MS = 200U;
        constexpr uint32_t CONTACTOR_PULL_IN_TIME_MS = 100U;
        constexpr uint32_t CONTACTOR_VALIDATION_TIME_MS = 200U;
        constexpr double PRECHARGE_RATIO = 0.95;
        constexpr double MAX_PRECHARGE_DELTA_V = 10.0;
        constexpr double MAX_SELF_TEST_LOAD_V = 30.0;
        constexpr double MIN_BATTERY_SOURCE_V = 30.0;
        constexpr double MIN_BATTERY_AGREEMENT_V = 10.0;
        constexpr double BATTERY_AGREEMENT_PERCENT = 0.05;
        constexpr uint16_t HV_FAULT_MASK =
            static_cast<uint16_t>(1U << SlaveController::HV_SUPERVISOR_FAULT);

        CAN *mCan = nullptr;
        PCC_STATE state = OFF;
        PCC_ERROR error = NO_ERROR;

        bool runRequest = false;
        bool requestEdgePending = false;
        bool requestRequiresRelease = false;
        bool faultClearPending = false;

        uint16_t activeErrors = HV_ERROR_NONE;
        uint16_t latchedErrors = HV_ERROR_NONE;
        uint16_t historicalErrors = HV_ERROR_NONE;

        double batteryVoltage = 0.0;
        double loadVoltage = 0.0;

        uint32_t stateStartTime = 0U;
        uint32_t stableStartTime = 0U;
        uint32_t lastPrechargeTime = 0U;

        void disableOutputs()
        {
            IO::setPrechargeRelay(false);
            IO::setContactorDutyPercent(0U);
        }

        double getBccPackVoltage()
        {
            return static_cast<double>(SlaveController::getPackVoltage()) / 1000000.0;
        }

        bool batteryVoltageAgreesWithBms()
        {
            const double bccVoltage = getBccPackVoltage();
            const double allowedDelta =
                std::max(MIN_BATTERY_AGREEMENT_V,
                         bccVoltage * BATTERY_AGREEMENT_PERCENT);
            return batteryVoltage >= MIN_BATTERY_SOURCE_V &&
                   std::abs(batteryVoltage - bccVoltage) <= allowedDelta;
        }

        bool isUsbOnly()
        {
            return IO::isUsbPresent() &&
                   batteryVoltage < MIN_BATTERY_SOURCE_V;
        }

        bool prechargeVoltageReached()
        {
            if (batteryVoltage < MIN_BATTERY_SOURCE_V)
            {
                return false;
            }

            const double delta = std::abs(batteryVoltage - loadVoltage);
            return loadVoltage >= batteryVoltage * PRECHARGE_RATIO &&
                   delta <= MAX_PRECHARGE_DELTA_V;
        }

        void publishAggregateActiveFault()
        {
            SlaveController::setHVSupervisorFaultActive(activeErrors != HV_ERROR_NONE);
        }

        void refreshActiveErrors()
        {
            uint16_t liveErrors = HV_ERROR_NONE;

            if ((latchedErrors & HV_ERROR_SENSOR_DIAGNOSTIC) != 0U &&
                !IO::areHVSensorDiagnosticsHealthy())
            {
                liveErrors |= HV_ERROR_SENSOR_DIAGNOSTIC;
            }
            if ((latchedErrors & HV_ERROR_USB_ONLY) != 0U && isUsbOnly())
            {
                liveErrors |= HV_ERROR_USB_ONLY;
            }
            if ((latchedErrors & HV_ERROR_BATTERY_VOLTAGE_MISMATCH) != 0U &&
                !batteryVoltageAgreesWithBms())
            {
                liveErrors |= HV_ERROR_BATTERY_VOLTAGE_MISMATCH;
            }
            if ((latchedErrors & HV_ERROR_LOAD_SIDE_ENERGIZED) != 0U &&
                loadVoltage >= MAX_SELF_TEST_LOAD_V)
            {
                liveErrors |= HV_ERROR_LOAD_SIDE_ENERGIZED;
            }

            activeErrors = liveErrors;
            publishAggregateActiveFault();
        }

        void latchFault(PCC_ERROR fault, uint16_t mask, bool conditionIsActive)
        {
            disableOutputs();
            state = OFF;
            requestEdgePending = false;
            requestRequiresRelease = true;
            error = fault;
            latchedErrors |= mask;
            historicalErrors |= mask;

            // Pulse the aggregate fault active so it is latched and recorded by
            // the BMS even for event faults such as a precharge timeout.
            SlaveController::setHVSupervisorFaultActive(true);
            activeErrors = conditionIsActive ? mask : HV_ERROR_NONE;
            publishAggregateActiveFault();

            PRINTF_ERR("[PCC] HV supervisor fault: error=%u active=%04X latched=%04X\n",
                       static_cast<unsigned>(fault),
                       activeErrors,
                       latchedErrors);
        }

        bool liveHVConditionsHealthy()
        {
            return IO::areHVSensorDiagnosticsHealthy() &&
                   !isUsbOnly() &&
                   batteryVoltageAgreesWithBms() &&
                   loadVoltage < MAX_SELF_TEST_LOAD_V;
        }

        bool validateSequencePlausibility()
        {
            if (!IO::areHVSensorDiagnosticsHealthy())
            {
                latchFault(SENSOR_DIAGNOSTIC_ERROR,
                           HV_ERROR_SENSOR_DIAGNOSTIC,
                           true);
                return false;
            }
            if (!batteryVoltageAgreesWithBms())
            {
                latchFault(BATTERY_VOLTAGE_MISMATCH,
                           HV_ERROR_BATTERY_VOLTAGE_MISMATCH,
                           true);
                return false;
            }
            return true;
        }

        void startPrecharge()
        {
            state = PRECHARGE;
            stateStartTime = millis();
            stableStartTime = 0U;
            IO::setContactorDutyPercent(0U);
            IO::setPrechargeRelay(true);
            PRINTF_INFO("[PCC] New state: PRECHARGE\n");
        }

        void handleSelfTest()
        {
            disableOutputs();

            if (!SlaveController::isHVReady())
            {
                state = OFF;
                requestRequiresRelease = true;
                return;
            }
            if (!IO::areHVSensorDiagnosticsHealthy())
            {
                latchFault(SENSOR_DIAGNOSTIC_ERROR,
                           HV_ERROR_SENSOR_DIAGNOSTIC,
                           true);
                return;
            }
            if (isUsbOnly())
            {
                latchFault(USB_ONLY_ERROR, HV_ERROR_USB_ONLY, true);
                return;
            }
            if (!batteryVoltageAgreesWithBms())
            {
                latchFault(BATTERY_VOLTAGE_MISMATCH,
                           HV_ERROR_BATTERY_VOLTAGE_MISMATCH,
                           true);
                return;
            }
            if (loadVoltage >= MAX_SELF_TEST_LOAD_V)
            {
                latchFault(LOAD_SIDE_ENERGIZED,
                           HV_ERROR_LOAD_SIDE_ENERGIZED,
                           true);
                return;
            }

            startPrecharge();
        }

        void handlePrecharge()
        {
            IO::setPrechargeRelay(true);
            IO::setContactorDutyPercent(0U);

            if (!validateSequencePlausibility())
            {
                return;
            }

            const uint32_t now = millis();
            if (now - stateStartTime >= PRECHARGE_TIMEOUT_MS)
            {
                latchFault(PRECHARGE_TIMEOUT,
                           HV_ERROR_PRECHARGE_TIMEOUT,
                           false);
                return;
            }

            if (!prechargeVoltageReached())
            {
                stableStartTime = 0U;
                return;
            }

            if (stableStartTime == 0U)
            {
                stableStartTime = now;
                return;
            }

            if (now - stableStartTime >= PRECHARGE_STABLE_TIME_MS)
            {
                lastPrechargeTime = now - stateStartTime;
                state = CONTACTOR_CLOSE;
                stateStartTime = now;
                stableStartTime = 0U;
                IO::setContactorDutyPercent(100U);
                PRINTF_INFO("[PCC] New state: CONTACTOR_CLOSE\n");
            }
        }

        void handleContactorClose()
        {
            if (!validateSequencePlausibility() ||
                !prechargeVoltageReached())
            {
                if (state != OFF)
                {
                    latchFault(PRECHARGE_VOLTAGE_LOST,
                               HV_ERROR_PRECHARGE_VOLTAGE_LOST,
                               false);
                }
                return;
            }

            const uint32_t elapsed = millis() - stateStartTime;
            if (elapsed < CONTACTOR_PULL_IN_TIME_MS)
            {
                IO::setPrechargeRelay(true);
                IO::setContactorDutyPercent(100U);
                return;
            }

            IO::setPrechargeRelay(false);
            IO::setContactorDutyPercent(HV_CONTACTOR_HOLD_DUTY_PERCENT);
            if (stableStartTime == 0U)
            {
                stableStartTime = millis();
            }

            if (millis() - stableStartTime >= CONTACTOR_VALIDATION_TIME_MS)
            {
                state = RUN;
                PRINTF_INFO("[PCC] New state: RUN\n");
            }
        }

        void handleRun()
        {
            IO::setPrechargeRelay(false);
            IO::setContactorDutyPercent(HV_CONTACTOR_HOLD_DUTY_PERCENT);

            if (!validateSequencePlausibility() ||
                !prechargeVoltageReached())
            {
                if (state != OFF)
                {
                    latchFault(CONTACTOR_VOLTAGE_LOST,
                               HV_ERROR_CONTACTOR_VOLTAGE_LOST,
                               false);
                }
            }
        }

        void handleSendingCANMessage()
        {
            static uint32_t lastSendTime = 0U;
            if (mCan == nullptr ||
                millis() - lastSendTime < DEFAULT_SETTINGS.CAN_PCC_PERIOD)
            {
                return;
            }

            lastSendTime = millis();
            CAN::Frame frame{};
            frame.id = DEFAULT_SETTINGS.CAN_PCC_ID;
            frame.length = 8U;
            frame.data[0] = static_cast<uint8_t>(state);
            frame.data[1] = static_cast<uint8_t>(error);
            frame.data16[1] = static_cast<uint16_t>(
                std::min<uint32_t>(lastPrechargeTime, UINT16_MAX));
            mCan->sendMessage(frame);
        }
    }

    void setup(CAN *can)
    {
        mCan = can;
        runRequest = false;
        requestEdgePending = false;
        requestRequiresRelease = false;
        faultClearPending = false;
        state = OFF;
        error = NO_ERROR;
        activeErrors = HV_ERROR_NONE;
        latchedErrors = HV_ERROR_NONE;
        historicalErrors = HV_ERROR_NONE;
        disableOutputs();
    }

    void setRunRequest(bool requested)
    {
        if (!requested)
        {
            runRequest = false;
            requestEdgePending = false;
            requestRequiresRelease = false;
            disableOutputs();
            state = OFF;
            return;
        }

        if (!runRequest && !requestRequiresRelease &&
            latchedErrors == HV_ERROR_NONE)
        {
            requestEdgePending = true;
        }
        runRequest = true;
    }

    bool requestFaultClear()
    {
        if (latchedErrors == HV_ERROR_NONE)
        {
            return true;
        }

        refreshActiveErrors();
        if (runRequest || activeErrors != HV_ERROR_NONE ||
            !liveHVConditionsHealthy())
        {
            PRINTF_ERR("[PCC] HV fault clear rejected\n");
            return false;
        }

        faultClearPending = true;
        return true;
    }

    void loop()
    {
        batteryVoltage = IO::getBatterySideVoltage();
        loadVoltage = IO::getLoadSideVoltage();

        if (faultClearPending &&
            (SlaveController::getLatchedFaults() & HV_FAULT_MASK) == 0U)
        {
            latchedErrors = HV_ERROR_NONE;
            activeErrors = HV_ERROR_NONE;
            error = NO_ERROR;
            faultClearPending = false;
            requestRequiresRelease = false;
            PRINTF_INFO("[PCC] HV fault clear completed\n");
        }

        if (latchedErrors != HV_ERROR_NONE)
        {
            refreshActiveErrors();
        }

        if (!runRequest ||
            (state != OFF && !SlaveController::isHVReady()))
        {
            disableOutputs();
            state = OFF;
            if (!runRequest)
            {
                requestRequiresRelease = false;
            }
        }

        switch (state)
        {
        case OFF:
            disableOutputs();
            if (requestEdgePending && latchedErrors == HV_ERROR_NONE)
            {
                requestEdgePending = false;
                state = SELF_TEST;
                PRINTF_INFO("[PCC] New state: SELF_TEST\n");
            }
            break;
        case SELF_TEST:
            handleSelfTest();
            break;
        case PRECHARGE:
            handlePrecharge();
            break;
        case CONTACTOR_CLOSE:
            handleContactorClose();
            break;
        case RUN:
            handleRun();
            break;
        default:
            disableOutputs();
            state = OFF;
            requestRequiresRelease = true;
            break;
        }

        handleSendingCANMessage();
    }

    PCC_STATE getPCCState()
    {
        return state;
    }

    PCC_ERROR getPCCError()
    {
        return error;
    }

    uint32_t getLastPrechargeTime()
    {
        return lastPrechargeTime;
    }

    uint16_t getActiveErrors()
    {
        return activeErrors;
    }

    uint16_t getLatchedErrors()
    {
        return latchedErrors;
    }

    uint16_t getHistoricalErrors()
    {
        return historicalErrors;
    }

    double getBatteryVoltage()
    {
        return batteryVoltage;
    }

    double getLoadVoltage()
    {
        return loadVoltage;
    }
}

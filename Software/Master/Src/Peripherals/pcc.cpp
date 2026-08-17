#include "pcc.h"

#include "BoardIO.h"
#include "FaultManager.h"
#include "Watchdog.h"
#include "TimeFunctions.h"
#include "bcc/SlaveController.h"

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
        PCC_STATE state = OFF;
        PCC_ERROR error = NO_ERROR;

        bool runRequest = false;
        bool firmwareUpdateLocked = false;

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

        void refreshActiveErrors()
        {
            const uint32_t latched = FaultManager::getSnapshot().hvLatched;
            if ((latched & (1UL << static_cast<uint8_t>(FaultManager::HvFault::SensorDiagnostic))) != 0U)
            {
                FaultManager::setHvFault(FaultManager::HvFault::SensorDiagnostic, !IO::areHVSensorDiagnosticsHealthy());
            }
            if ((latched & (1UL << static_cast<uint8_t>(FaultManager::HvFault::BatteryVoltageMismatch))) != 0U)
            {
                FaultManager::setHvFault(FaultManager::HvFault::BatteryVoltageMismatch, !batteryVoltageAgreesWithBms());
            }
            if ((latched & (1UL << static_cast<uint8_t>(FaultManager::HvFault::LoadSideEnergized))) != 0U)
            {
                FaultManager::setHvFault(FaultManager::HvFault::LoadSideEnergized, loadVoltage >= MAX_SELF_TEST_LOAD_V);
            }
        }

        void latchFault(PCC_ERROR fault, FaultManager::HvFault faultId, bool conditionIsActive)
        {
            error = fault;
            FaultManager::setHvFault(faultId, true);
            if (!conditionIsActive)
            {
                FaultManager::setHvFault(faultId, false);
            }

            PRINTF_ERR("[PCC] HV fault: error=%u active=%u\n",
                       static_cast<unsigned>(fault),
                       conditionIsActive ? 1U : 0U);
        }

        bool validateSequencePlausibility()
        {
            if (!IO::areHVSensorDiagnosticsHealthy())
            {
                latchFault(SENSOR_DIAGNOSTIC_ERROR,
                           FaultManager::HvFault::SensorDiagnostic,
                           true);
                return false;
            }
            if (!batteryVoltageAgreesWithBms())
            {
                latchFault(BATTERY_VOLTAGE_MISMATCH,
                           FaultManager::HvFault::BatteryVoltageMismatch,
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
                return;
            }
            if (!IO::areHVSensorDiagnosticsHealthy())
            {
                latchFault(SENSOR_DIAGNOSTIC_ERROR,
                           FaultManager::HvFault::SensorDiagnostic,
                           true);
                return;
            }
            if (!batteryVoltageAgreesWithBms())
            {
                latchFault(BATTERY_VOLTAGE_MISMATCH,
                           FaultManager::HvFault::BatteryVoltageMismatch,
                           true);
                return;
            }
            if (loadVoltage >= MAX_SELF_TEST_LOAD_V)
            {
                latchFault(LOAD_SIDE_ENERGIZED,
                           FaultManager::HvFault::LoadSideEnergized,
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
                           FaultManager::HvFault::PrechargeTimeout,
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
                               FaultManager::HvFault::PrechargeVoltageLost,
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
                               FaultManager::HvFault::ContactorVoltageLost,
                               false);
                }
            }
        }

    }

    void setup()
    {
        runRequest = false;
        firmwareUpdateLocked = false;
        state = OFF;
        error = NO_ERROR;
        disableOutputs();
    }

    void setRunRequest(bool requested)
    {
        if (requested && firmwareUpdateLocked)
        {
            PRINTF_WARN("[PCC] Run request denied during firmware update\n");
            return;
        }

        if (!requested)
        {
            runRequest = false;
            disableOutputs();
            state = OFF;
            return;
        }

        runRequest = true;
    }

    void forceSafeOffFromFaultManager()
    {
        disableOutputs();
        state = OFF;
        FaultManager::setHvRunning(false);
    }

    bool isSafeForFirmwareUpdate()
    {
        return !firmwareUpdateLocked && !runRequest && state == OFF;
    }

    bool enterFirmwareUpdateLock()
    {
        if (!isSafeForFirmwareUpdate())
        {
            return false;
        }

        firmwareUpdateLocked = true;
        disableOutputs();
        state = OFF;
        return true;
    }

    bool isFirmwareUpdateLocked()
    {
        return firmwareUpdateLocked;
    }

    bool isRunRequested()
    {
        return runRequest;
    }

    void loop()
    {
		Watchdog::reportPccProgress();
        batteryVoltage = IO::getBatterySideVoltage();
        loadVoltage = IO::getLoadSideVoltage();

        if (firmwareUpdateLocked)
        {
            disableOutputs();
            state = OFF;
            return;
        }

        refreshActiveErrors();

        if (!runRequest)
        {
            disableOutputs();
            state = OFF;
        }

        switch (state)
        {
        case OFF:
            disableOutputs();
            if (runRequest && FaultManager::canEnableHv())
            {
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
            break;
        }

        FaultManager::setHvRunning(state == RUN);
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

    double getBatteryVoltage()
    {
        return batteryVoltage;
    }

    double getLoadVoltage()
    {
        return loadVoltage;
    }
}

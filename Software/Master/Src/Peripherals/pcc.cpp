#include "pcc.h"

#include "BoardIO.h"
#include "FaultManager.h"
#include "Watchdog.h"
#include "TimeFunctions.h"
#include "bcc/SlaveController.h"
#include "FreeRTOS.h"

#include <algorithm>
#include <atomic>
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
        constexpr double PRECHARGE_RATIO = 0.88;
        constexpr double MAX_SELF_TEST_LOAD_V = 30.0;
        constexpr double MIN_BATTERY_SOURCE_V = 30.0;
        constexpr double MIN_BATTERY_AGREEMENT_V = 10.0;
        constexpr double BATTERY_AGREEMENT_PERCENT = 0.05;
        constexpr uint32_t FIRMWARE_UPDATE_PREPARE_TIMEOUT_MS = 3'000U;
        std::atomic<PCC_STATE> state{OFF};
        std::atomic<PCC_ERROR> error{NO_ERROR};

        std::atomic<bool> runRequest{false};
        std::atomic<bool> firmwareUpdatePrepared{false};
        std::atomic<bool> firmwareUpdateLocked{false};
        std::atomic<uint32_t> firmwareUpdatePrepareDeadlineMs{0U};
        std::atomic<bool> hvVoltagesValid{false};

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
            // An invalid ADC snapshot is a sensor-data condition, not a real
            // zero-voltage measurement and must not become a pack mismatch.
            if (!hvVoltagesValid.load(std::memory_order_acquire))
            {
                return true;
            }
            const double bccVoltage = getBccPackVoltage();
            const bool batteryPresent = batteryVoltage >= MIN_BATTERY_SOURCE_V;
            const bool bccPresent = bccVoltage >= MIN_BATTERY_SOURCE_V;
            if (!batteryPresent && !bccPresent)
            {
                return true;
            }
            if (batteryPresent != bccPresent)
            {
                return false;
            }
            const double allowedDelta =
                std::max(MIN_BATTERY_AGREEMENT_V,
                         bccVoltage * BATTERY_AGREEMENT_PERCENT);
            return std::abs(batteryVoltage - bccVoltage) <= allowedDelta;
        }

        bool prechargeVoltageReached()
        {
            if (batteryVoltage < MIN_BATTERY_SOURCE_V)
            {
                return false;
            }

            return loadVoltage >= batteryVoltage * PRECHARGE_RATIO;
        }

        void refreshActiveErrors()
        {
            const uint32_t latched = FaultManager::getSnapshot().hvLatched;
            const bool batteryMismatch = hvVoltagesValid.load(std::memory_order_acquire) &&
                                         !batteryVoltageAgreesWithBms();

            // A pack-voltage disagreement remains visible while deliberately
            // OFF, but becomes a blocking HV fault only for a run request.
            FaultManager::setWarning(FaultManager::Warning::BatteryVoltageMismatchOff,
                                     !runRequest && batteryMismatch);
            if (!runRequest)
            {
                FaultManager::setHvFault(FaultManager::HvFault::BatteryVoltageMismatch, false);
            }
            if ((latched & (1UL << static_cast<uint8_t>(FaultManager::HvFault::SensorDiagnostic))) != 0U)
            {
                FaultManager::setHvFault(FaultManager::HvFault::SensorDiagnostic,
                                         !hvVoltagesValid.load(std::memory_order_acquire) ||
                                             !IO::areHVSensorDiagnosticsHealthy());
            }
            if (runRequest &&
                (latched & (1UL << static_cast<uint8_t>(FaultManager::HvFault::BatteryVoltageMismatch))) != 0U)
            {
                FaultManager::setHvFault(FaultManager::HvFault::BatteryVoltageMismatch, batteryMismatch);
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
            if (!hvVoltagesValid.load(std::memory_order_acquire))
            {
                latchFault(SENSOR_DIAGNOSTIC_ERROR,
                           FaultManager::HvFault::SensorDiagnostic,
                           true);
                return false;
            }
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
            if (!hvVoltagesValid.load(std::memory_order_acquire))
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
        firmwareUpdatePrepared = false;
        firmwareUpdateLocked = false;
        firmwareUpdatePrepareDeadlineMs = 0U;
        hvVoltagesValid = false;
        state = OFF;
        error = NO_ERROR;
        disableOutputs();
    }

    void setRunRequest(bool requested)
    {
        taskENTER_CRITICAL();
        if (requested && (firmwareUpdatePrepared.load(std::memory_order_relaxed) ||
                          firmwareUpdateLocked.load(std::memory_order_relaxed)))
        {
            taskEXIT_CRITICAL();
            PRINTF_WARN("[PCC] Run request denied during firmware update\n");
            return;
        }

        if (!requested)
        {
            runRequest.store(false, std::memory_order_release);
            state.store(OFF, std::memory_order_release);
            taskEXIT_CRITICAL();
            disableOutputs();
            FaultManager::setHvFault(FaultManager::HvFault::BatteryVoltageMismatch, false);
            return;
        }

        runRequest.store(true, std::memory_order_release);
        taskEXIT_CRITICAL();
    }

    void forceSafeOffFromFaultManager()
    {
        disableOutputs();
        state.store(OFF, std::memory_order_release);
        FaultManager::setHvRunning(false);
    }

    bool isSafeForFirmwareUpdate()
    {
        taskENTER_CRITICAL();
        const bool safe = !firmwareUpdatePrepared.load(std::memory_order_relaxed) &&
                          !firmwareUpdateLocked.load(std::memory_order_relaxed) &&
                          !runRequest.load(std::memory_order_relaxed) &&
                          state.load(std::memory_order_relaxed) == OFF;
        taskEXIT_CRITICAL();
        return safe;
    }

    bool prepareFirmwareUpdate()
    {
        taskENTER_CRITICAL();
        if (firmwareUpdatePrepared.load(std::memory_order_relaxed) ||
            firmwareUpdateLocked.load(std::memory_order_relaxed) ||
            runRequest.load(std::memory_order_relaxed) ||
            state.load(std::memory_order_relaxed) != OFF)
        {
            taskEXIT_CRITICAL();
            return false;
        }

        firmwareUpdatePrepared.store(true, std::memory_order_release);
        firmwareUpdatePrepareDeadlineMs.store(HAL_GetTick() + FIRMWARE_UPDATE_PREPARE_TIMEOUT_MS,
                                              std::memory_order_release);
        state.store(OFF, std::memory_order_release);
        taskEXIT_CRITICAL();
        disableOutputs();
        FaultManager::setHvRunning(false);
        return true;
    }

    bool commitFirmwareUpdate()
    {
        taskENTER_CRITICAL();
        if (!firmwareUpdatePrepared.load(std::memory_order_relaxed) ||
            static_cast<int32_t>(HAL_GetTick() - firmwareUpdatePrepareDeadlineMs.load(std::memory_order_relaxed)) >= 0)
        {
            firmwareUpdatePrepared.store(false, std::memory_order_release);
            taskEXIT_CRITICAL();
            return false;
        }

        firmwareUpdatePrepared.store(false, std::memory_order_release);
        firmwareUpdateLocked.store(true, std::memory_order_release);
        state.store(OFF, std::memory_order_release);
        taskEXIT_CRITICAL();
        disableOutputs();
        FaultManager::setHvRunning(false);
        return true;
    }

    bool isFirmwareUpdatePrepared()
    {
        return firmwareUpdatePrepared.load(std::memory_order_acquire);
    }

    bool isFirmwareUpdateLocked()
    {
        return firmwareUpdateLocked.load(std::memory_order_acquire);
    }

    bool isRunRequested()
    {
        return runRequest.load(std::memory_order_acquire);
    }

    void loop()
    {
		Watchdog::reportPccProgress();
        const IO::HVVoltages hvVoltages = IO::getHVVoltages();
        hvVoltagesValid.store(hvVoltages.valid, std::memory_order_release);
        if (hvVoltages.valid)
        {
            batteryVoltage = hvVoltages.batteryVoltage;
            loadVoltage = hvVoltages.loadVoltage;
        }

        if (firmwareUpdateLocked.load(std::memory_order_acquire))
        {
            disableOutputs();
            state = OFF;
            return;
        }

        if (firmwareUpdatePrepared.load(std::memory_order_acquire))
        {
            disableOutputs();
            state = OFF;
            FaultManager::setHvRunning(false);
            if (static_cast<int32_t>(HAL_GetTick() - firmwareUpdatePrepareDeadlineMs.load(std::memory_order_acquire)) >= 0)
            {
                firmwareUpdatePrepared.store(false, std::memory_order_release);
            }
            return;
        }

        refreshActiveErrors();

        if (!runRequest.load(std::memory_order_acquire))
        {
            disableOutputs();
            state = OFF;
        }

        switch (state)
        {
        case OFF:
            disableOutputs();
            if (runRequest.load(std::memory_order_acquire) && FaultManager::canEnableHv())
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

        FaultManager::setHvRunning(state.load(std::memory_order_acquire) == RUN);
    }

    PCC_STATE getPCCState()
    {
        return state.load(std::memory_order_acquire);
    }

    PCC_ERROR getPCCError()
    {
        return error.load(std::memory_order_acquire);
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

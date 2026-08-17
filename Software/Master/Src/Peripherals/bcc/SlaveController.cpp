
/*!
 * @file slaveController.cpp
 *
 * Controller for the slave devices in the TPL chain.
 */

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "TimeFunctions.h"
#include "FaultManager.h"
#include "Watchdog.h"
#include "bcc/SlaveController.h"
#include "bcc/bcc_diagnostics.h"
#include "bcc/UserSettings.h"
#include "USBCOM.h"
#include "FreeRTOS.h"
#include <array>
#include <atomic>

#define DEBUG_LVL 2
#include "Debug.h"

using std::vector;

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/* NO NEED TO CHANGE THE SETTINGS BELOW */


/*! @brief Time after VPWR connection for the IC to be ready for initialization
 *  (t_VPWR(READY), max.) in [ms]. */
#define BCC_T_VPWR_READY_MS 5U
#define CID_INITIALIZATION_MAX_FAILURES 3U

namespace SlaveController
{
    namespace
    {
        /*******************************************************************************
            FreeRTOS Task stuff
        ******************************************************************************/
        osThreadId_t bmsTaskHandle;
        const osThreadAttr_t bmsTask_attributes = {
            .name = "bmsTask",
            .stack_size = 2048,
            .priority = (osPriority_t)osPriorityNormal,
        };

        /*******************************************************************************
         * Private variables
         ******************************************************************************/

        CAN *mCAN = nullptr; /* Pointer to the CAN instance */

        uint8_t registerInitializationFailureCount = 0U;
        uint8_t diagnosticsFailureCount = 0U;
        uint8_t tplInitializationFailureCount = 0U;
		uint32_t initializationRetryNotBeforeMs = 0U;
        std::array<bool, static_cast<size_t>(COMMUNICATION_TIMEOUT) + 1U> bmsFaultInputs{};

        BMSState currentState = DEVICE_INITIALIZATION; // Current state of the BMS

        static vector<BCC> mSlaves; /* Array of BCC devices */
        vector<uint8_t> cidInitializationFailureCounts;

        UserSettings_t settings;

        uint16_t cBalancingTime = 0; // Time in [min] for a single cell balancing operation

        // Deliberately false in this development build. Change only with the
        // production balancing enablement decision documented in uart-v1.md.
        std::atomic<bool> balancingRequested{false};

        uint8_t currentMeasurementSlaveIdx = 0; // Index of the slave responsible for current measurement

        std::atomic<uint32_t> measurementSequence{0};
        bool completeMeasurementSetValid = false;

        vector<uint16_t> ICtemperatures; // Vector of IC temperatures
        uint16_t minICtemperature = UINT16_MAX;
        uint16_t maxICtemperature = 0;
        vector<vector<uint16_t>> NTCtemperatures; // 2D vector of NTC temperatures
        uint16_t minNTCtemperature = UINT16_MAX;
        uint16_t maxNTCtemperature = 0;

        vector<vector<uint32_t>> cellVoltages; // 2D vector of cell voltages
        uint32_t minCellVoltage = UINT32_MAX;
        uint32_t maxCellVoltage = 0;
        uint32_t packVoltage = 0;
        double packCurrent = 0.0;

        RegisterRequest registerRequest = {};
        bool *registerRequestFlag = nullptr;
        RegisterReponse *registerResponse = nullptr;
        bool registerRequestBusy = false;

        /*******************************************************************************
         * Private functions
         ******************************************************************************/

        bool measurementsAreFresh()
        {
            if (!completeMeasurementSetValid)
            {
                return false;
            }

            for (auto &slave : mSlaves)
            {
                if ((millis() - slave.getTimeReceivedLastMeasurement()) > settings.SAFETY_LIMITS.COMMUNICATION_TIMEOUT)
                {
                    return false;
                }
            }
            return true;
        }

        void setState(BMSState state)
        {
            if (state != RUNNING)
            {
                completeMeasurementSetValid = false;
            }
            currentState = state;
            switch (state)
            {
            case DEVICE_INITIALIZATION:
                PRINTF_INFO("[SC] New state: DEVICE_INITIALIZATION\n");
                break;
            case REGISTER_INITIALIZATION:
                PRINTF_INFO("[SC] New state: REGISTER_INITIALIZATION\n");
                break;
            case PERFORMING_DIAGNOSTICS:
                PRINTF_INFO("[SC] New state: PERFORMING_DIAGNOSTICS\n");
                break;
            case RUNNING:
                PRINTF_INFO("[SC] New state: RUNNING\n");
                break;
            case CRITICAL:
                PRINTF_INFO("[SC] New state: CRITICAL\n");
                break;
            }
        }

        FaultManager::BmsFault mapFault(BMSFault fault)
        {
            switch (fault)
            {
            case INVALID_CONFIG:
                return FaultManager::BmsFault::ConfigurationInvalid;
            case TPL_FAULT:
            case CID_INITIALIZATION_FAULT:
            case REGISTER_INITIALIZATION_FAULT:
                return FaultManager::BmsFault::SlaveUnavailable;
            case CELL_BALANCING_FAULT:
                return FaultManager::BmsFault::BalancingHardwareFault;
            case DIAGNOSTICS_FAULT:
                return FaultManager::BmsFault::BccDiagnostics;
            case OVERVOLTAGE_LIMIT:
            case UNDERVOLTAGE_LIMIT:
                return FaultManager::BmsFault::CellVoltageLimit;
            case TEMPERATURE_LIMIT:
            case IC_TEMPERATURE:
                return FaultManager::BmsFault::ThermalLimit;
            case OVERCURRENT_LIMIT:
                return FaultManager::BmsFault::CurrentLimit;
            case OPEN_SHORT_FAULT:
            case SYSTEM_FAULT:
                return FaultManager::BmsFault::BccIntegrity;
            case COMMUNICATION_TIMEOUT:
                return FaultManager::BmsFault::BccCommunication;
            }
            return FaultManager::BmsFault::BccIntegrity;
        }

        void updateFault(BMSFault fault, bool active)
        {
            bmsFaultInputs[static_cast<size_t>(fault)] = active;
            const FaultManager::BmsFault mappedFault = mapFault(fault);

            bool mappedActive = false;
            for (size_t index = 0U; index < bmsFaultInputs.size(); ++index)
            {
                if (bmsFaultInputs[index] &&
                    mapFault(static_cast<BMSFault>(index)) == mappedFault)
                {
                    mappedActive = true;
                    break;
                }
            }
            FaultManager::setBmsFault(mappedFault, mappedActive);
        }

        // Checks if all CIDs are present
        bool allCIDsPresence()
        {
            // Loop over slaves
            for (auto &slave : mSlaves)
            {
                // Check if the CID is present
                if (!slave.isPresent())
                {
                    PRINTF_ERR("[SC] Cannot communicate with CID %d\n", slave.getCID());
                    return false;
                }
            }
            return true;
        }

        vector<bcc_init_reg_t> getInitGlobalRegisterMapping()
        {

            uint16_t ov_limit_mv = settings.SAFETY_LIMITS.OVERVOLTAGE_LIMIT * 1000;
            uint16_t uv_limit_mv = settings.SAFETY_LIMITS.UNDERVOLTAGE_LIMIT * 1000;

            uint16_t ot_an_reg_val = BCC_TEMP_TO_VOLT(settings.SAFETY_LIMITS.OVERTEMPERATURE_LIMIT, settings.NTC_RESISTANCE, settings.NTC_BETA);
            uint16_t ut_an_reg_val = BCC_TEMP_TO_VOLT(settings.SAFETY_LIMITS.UNDERTEMPERATURE_LIMIT, settings.NTC_RESISTANCE, settings.NTC_BETA);

            return {
                {MC33771C_GPIO_CFG1_OFFSET, MC33771C_GPIO_CFG1_POR_VAL, MC33771C_GPIO_CFG1_VALUE},
                {MC33771C_GPIO_CFG2_OFFSET, MC33771C_GPIO_CFG2_POR_VAL, MC33771C_GPIO_CFG2_VALUE},
                {MC33771C_TH_ALL_CT_OFFSET, MC33771C_TH_ALL_CT_POR_VAL, (uint16_t)MC33771C_TH_ALL_CT_VALUE(ov_limit_mv, uv_limit_mv)},
                {MC33771C_TH_AN6_OT_OFFSET, MC33771C_TH_AN6_OT_POR_VAL, (uint16_t)MC33771C_TH_ANX_OT_VALUE(ot_an_reg_val)},
                {MC33771C_TH_AN5_OT_OFFSET, MC33771C_TH_AN5_OT_POR_VAL, (uint16_t)MC33771C_TH_ANX_OT_VALUE(ot_an_reg_val)},
                {MC33771C_TH_AN4_OT_OFFSET, MC33771C_TH_AN4_OT_POR_VAL, (uint16_t)MC33771C_TH_ANX_OT_VALUE(ot_an_reg_val)},
                {MC33771C_TH_AN3_OT_OFFSET, MC33771C_TH_AN3_OT_POR_VAL, (uint16_t)MC33771C_TH_ANX_OT_VALUE(ot_an_reg_val)},
                {MC33771C_TH_AN2_OT_OFFSET, MC33771C_TH_AN2_OT_POR_VAL, (uint16_t)MC33771C_TH_ANX_OT_VALUE(ot_an_reg_val)},
                {MC33771C_TH_AN1_OT_OFFSET, MC33771C_TH_AN1_OT_POR_VAL, (uint16_t)MC33771C_TH_ANX_OT_VALUE(ot_an_reg_val)},
                {MC33771C_TH_AN0_OT_OFFSET, MC33771C_TH_AN0_OT_POR_VAL, (uint16_t)MC33771C_TH_ANX_OT_VALUE(ot_an_reg_val)},
                {MC33771C_TH_AN6_UT_OFFSET, MC33771C_TH_AN6_UT_POR_VAL, (uint16_t)MC33771C_TH_ANX_UT_VALUE(ut_an_reg_val)},
                {MC33771C_TH_AN5_UT_OFFSET, MC33771C_TH_AN5_UT_POR_VAL, (uint16_t)MC33771C_TH_ANX_UT_VALUE(ut_an_reg_val)},
                {MC33771C_TH_AN4_UT_OFFSET, MC33771C_TH_AN4_UT_POR_VAL, (uint16_t)MC33771C_TH_ANX_UT_VALUE(ut_an_reg_val)},
                {MC33771C_TH_AN3_UT_OFFSET, MC33771C_TH_AN3_UT_POR_VAL, (uint16_t)MC33771C_TH_ANX_UT_VALUE(ut_an_reg_val)},
                {MC33771C_TH_AN2_UT_OFFSET, MC33771C_TH_AN2_UT_POR_VAL, (uint16_t)MC33771C_TH_ANX_UT_VALUE(ut_an_reg_val)},
                {MC33771C_TH_AN1_UT_OFFSET, MC33771C_TH_AN1_UT_POR_VAL, (uint16_t)MC33771C_TH_ANX_UT_VALUE(ut_an_reg_val)},
                {MC33771C_TH_AN0_UT_OFFSET, MC33771C_TH_AN0_UT_POR_VAL, (uint16_t)MC33771C_TH_ANX_UT_VALUE(ut_an_reg_val)},
                {MC33771C_SYS_CFG1_OFFSET, MC33771C_SYS_CFG1_POR_VAL, MC33771C_SYS_CFG1_VALUE(false)}, // Slave with Current measurement should have different initial register
                {MC33771C_SYS_CFG2_OFFSET, MC33771C_SYS_CFG2_POR_VAL, MC33771C_SYS_CFG2_VALUE},
                {MC33771C_ADC_CFG_OFFSET, MC33771C_ADC_CFG_POR_VAL, MC33771C_ADC_CFG_VALUE},
                {MC33771C_ADC2_OFFSET_COMP_OFFSET, MC33771C_ADC2_OFFSET_COMP_POR_VAL, MC33771C_ADC2_OFFSET_COMP_VALUE}, // Only useful if I_MEAS is enabled, but global write is probably fine
                {MC33771C_FAULT_MASK1_OFFSET, MC33771C_FAULT_MASK1_POR_VAL, MC33771C_FAULT_MASK1_VALUE},
                {MC33771C_FAULT_MASK2_OFFSET, MC33771C_FAULT_MASK2_POR_VAL, MC33771C_FAULT_MASK2_VALUE},
                {MC33771C_FAULT_MASK3_OFFSET, MC33771C_FAULT_MASK3_POR_VAL, MC33771C_FAULT_MASK3_VALUE},
            };
        }

        /*! @brief This function initializes registers of all devices in the TPL chain.
         *  Based on the example in the programming guide.
         *
         * @return bool True if the initialization was successful, false otherwise.
         */
        bool initializeRegisters()
        {
            bcc_status_t status;

            vector<bcc_init_reg_t> globalRegisters = getInitGlobalRegisterMapping();

            // Write global registers
            for (uint16_t i = 0; i < globalRegisters.size(); i++)
            {
                if (globalRegisters[i].value != globalRegisters[i].defaultVal)
                {
                    if ((status = BCC::regWriteGlobal(globalRegisters[i].address, globalRegisters[i].value)) != BCC_STATUS_SUCCESS)
                    {
                        // TODO this: reportBCCError(status, 0);
                        return false;
                    }
                }
            }

            // Loop trough all devices to set slave specific registers based on config
            for (auto &slave : mSlaves)
            {
                if (slave.currentSenseEnabled())
                {
                    // Set I_MEAS_EN in SYS_CFG1
                    if ((status = slave.regWrite(MC33771C_SYS_CFG1_OFFSET, MC33771C_SYS_CFG1_VALUE(true))) != BCC_STATUS_SUCCESS)
                    {
                        return false;
                    }
                }

                // Depending on number of NTCs used, set unused GPIOs to digital in, (and highest bits to 0)
                uint16_t regValue = (0x2AAA << 2 * slave.getNTCCount()) & 0x3FFF;
                if ((status = slave.regWrite(MC33771C_GPIO_CFG1_OFFSET, regValue)) != BCC_STATUS_SUCCESS)
                {
                    return false;
                }

                // Disable OV/UV detection for unused cells  (reg OV_UV_EN)
                regValue = slave.getCellMap();

                // Set Common OV/UV threshold bits
                regValue |= 0xC000U;

                if ((status = slave.regWrite(MC33771C_OV_UV_EN_OFFSET, regValue)) != BCC_STATUS_SUCCESS)
                {
                    return false;
                }
            }

            // Clear fault bits
            if ((status = BCC::regWriteGlobal(MC33771C_CELL_OV_FLT_OFFSET, 0x0000U)) != BCC_STATUS_SUCCESS ||
                (status = BCC::regWriteGlobal(MC33771C_CELL_UV_FLT_OFFSET, 0x0000U)) != BCC_STATUS_SUCCESS ||
                (status = BCC::regWriteGlobal(MC33771C_AN_OT_UT_FLT_OFFSET, 0x0000U)) != BCC_STATUS_SUCCESS ||
                (status = BCC::regWriteGlobal(MC33771C_FAULT1_STATUS_OFFSET, 0x0000U)) != BCC_STATUS_SUCCESS ||
                (status = BCC::regWriteGlobal(MC33771C_FAULT2_STATUS_OFFSET, 0x0000U)) != BCC_STATUS_SUCCESS ||
                (status = BCC::regWriteGlobal(MC33771C_FAULT3_STATUS_OFFSET, 0x0000U)) != BCC_STATUS_SUCCESS)
            {
                // TODO this: reportBCCError(status, 0);
                return false;
            }

            return true;
        };

        bool startMeasurements()
        {
            bool success = true;

            // Pause CB for all slaves
            for (auto &slave : mSlaves)
            {
                slave.CB_Pause(true);
            }

            // It is recommended to wait for at least 3ms before starting the ADC conversion
            // to ensure that the LP filters have settled.
            delay(3);

            if (BCC::meas_StartConversionGlobal() != BCC_STATUS_SUCCESS)
            {
                success = false;
            }

            // Wait for the conversion to finish
            for (auto &slave : mSlaves)
            {
                if (slave.meas_WaitOnConversion(BCC_ADC_AVG) != BCC_STATUS_SUCCESS)
                {
                    PRINTF_WARN("[SC] Slave (CID: %u) failed on: wait on conversion\n", slave.getCID());
                    success = false;
                }
            }

            // Resume CB
            for (auto &slave : mSlaves)
            {
                slave.CB_Pause(false);
            }

            return success;
        };

        bool faultDetection()
        {
            uint16_t combinedFaults[BCC_STAT_CNT] = {0};
            bool faultStatusSuccessful = true;
            for (auto &slave : mSlaves)
            {
                if (slave.fault_GetStatus(combinedFaults) != BCC_STATUS_SUCCESS)
                {
                    PRINTF_WARN("[SC] Slave (CID: %u) failed on: get fault status\n", slave.getCID());
                    faultStatusSuccessful = false;
                }

                // TODO Log all fault registers in FLASH or something
            }

            double ampHour = 0.0;
            double current = 0.0;
            if (mSlaves[currentMeasurementSlaveIdx].meas_GetAmpHourAndIAvg(
                    settings.SHUNT_RESISTANCE,
                    settings.INVERT_CURRENT,
                    &ampHour,
                    &current) != BCC_STATUS_SUCCESS)
            {
                PRINTF_WARN("[SC] Current measurement failed on CID %u\n",
                            mSlaves[currentMeasurementSlaveIdx].getCID());
                faultStatusSuccessful = false;
            }
            else
            {
                packCurrent = current;
                updateFault(
                    OVERCURRENT_LIMIT,
                    current < -settings.SAFETY_LIMITS.DISCHARGE_CURRENT_LIMIT ||
                        current > settings.SAFETY_LIMITS.CHARGE_CURRENT_LIMIT);
            }

            if (!faultStatusSuccessful)
            {
                return false;
            }

            updateFault(OVERVOLTAGE_LIMIT, combinedFaults[BCC_FS_CELL_OV] != 0);
            updateFault(UNDERVOLTAGE_LIMIT, combinedFaults[BCC_FS_CELL_UV] != 0);
            updateFault(TEMPERATURE_LIMIT, combinedFaults[BCC_FS_AN_OT_UT] != 0);
            updateFault(OPEN_SHORT_FAULT, combinedFaults[BCC_FS_CB_OPEN] != 0 || combinedFaults[BCC_FS_CB_SHORT] != 0 || combinedFaults[BCC_FS_GPIO_SHORT]);
            updateFault(IC_TEMPERATURE, (combinedFaults[BCC_FS_FAULT2] & MC33771C_FAULT2_STATUS_IC_TSD_FLT_MASK) != 0);
            updateFault(SYSTEM_FAULT, combinedFaults[BCC_FS_FAULT1] != 0 || combinedFaults[BCC_FS_FAULT2] != 0 || combinedFaults[BCC_FS_FAULT3] != 0);
            return true;
        };

        bool getMeasurements(bool forceRead = false)
        {
            bcc_status_t status;
            static uint32_t loopCount = 0;

            // Only fetch measurements every x loops
            if (!forceRead && (loopCount++ % settings.BMS_MEASUREMENT_PERIOD_FACTOR) != 0)
            {
                return completeMeasurementSetValid;
            }

            bool success = true;

            // PRINTF_INFO("[SC] Fetching measurements\n");
            for (auto &slave : mSlaves)
            {
                // Fetch the measurements from the BCC
                if ((status = slave.meas_GetRawValues()) != BCC_STATUS_SUCCESS)
                {
                    PRINTF_WARN("[SC] Slave (CID: %u) failed on: get raw values\n", slave.getCID());
                    success = false;
                }
            }

            // Reset stats
            minCellVoltage = UINT32_MAX;
            maxCellVoltage = 0;
            minNTCtemperature = UINT16_MAX;
            maxNTCtemperature = 0;
            minICtemperature = UINT16_MAX;
            maxICtemperature = 0;
            packVoltage = 0;

            // Gather all cell voltages & temperatures and put them into static 2D arrays
            for (size_t i = 0; i < getNumOfSlaves(); i++)
            {
                // Get the cell voltages
                if (mSlaves[i].meas_GetCellVoltages(cellVoltages[i]) != BCC_STATUS_SUCCESS)
                {
                    PRINTF_WARN("[SC] Slave (CID: %u) failed on: get cell voltages\n", mSlaves[i].getCID());
                    success = false;
                    continue;
                }

                // Get the NTC temperatures
                if (mSlaves[i].meas_GetNTCTemperatures(NTCtemperatures[i], settings.NTC_RESISTANCE, settings.NTC_BETA) != BCC_STATUS_SUCCESS)
                {
                    PRINTF_WARN("[SC] Slave (CID: %u) failed on: get NTC temperatures\n", mSlaves[i].getCID());
                    success = false;
                    continue;
                }

                // Also get the IC temperature
                if (mSlaves[i].meas_GetIcTemperature(&ICtemperatures[i]) != BCC_STATUS_SUCCESS)
                {
                    PRINTF_WARN("[SC] Slave (CID: %u) failed on: get IC temperature\n", mSlaves[i].getCID());
                    success = false;
                    continue;
                }

                // Update some cell voltage stats
                for (auto &cellVoltage : cellVoltages[i])
                {
                    if (cellVoltage < minCellVoltage)
                    {
                        minCellVoltage = cellVoltage;
                    }
                    if (cellVoltage > maxCellVoltage)
                    {
                        maxCellVoltage = cellVoltage;
                    }
                    packVoltage += cellVoltage;
                }

                // Update some NTC temperature stats
                for (auto &NTCtemperature : NTCtemperatures[i])
                {
                    if (NTCtemperature < minNTCtemperature)
                    {
                        minNTCtemperature = NTCtemperature;
                    }
                    if (NTCtemperature > maxNTCtemperature)
                    {
                        maxNTCtemperature = NTCtemperature;
                    }
                }

                // Update IC temperature stats
                if (ICtemperatures[i] > maxICtemperature)
                {
                    maxICtemperature = ICtemperatures[i];
                }
                if (ICtemperatures[i] < minICtemperature)
                {
                    minICtemperature = ICtemperatures[i];
                }
            }

            // Update the SoC if necessary
            if (success && settings.AUTO_CALIBRATE_SOC)
            {
                // If the pack voltage is above the threshold, set the SoC to 100%
                const uint32_t calibrateVoltage = settings.AUTO_CALIBRATE_SOC_THRESHOLD * settings.SAFETY_LIMITS.OVERVOLTAGE_LIMIT * getCellCount() * 1'000'000U;
                if (getPackVoltage() > calibrateVoltage)
                {
                    setSoC(BCC_SOC_TO_SOCRAW(1));
                }
            }

            completeMeasurementSetValid = success;
            return success;
        }

        void doCommunicationCheck(bool faultStatusSuccessful)
        {
            bool communicationFault = !measurementsAreFresh() || !faultStatusSuccessful;

            if (!communicationFault && !allCIDsPresence())
            {
                communicationFault = true;
            }

            updateFault(COMMUNICATION_TIMEOUT, communicationFault);
        }

        // Balancing is allowed only with a healthy, fully running BMS.
        bool isBalancingAllowed()
        {
            const FaultManager::Snapshot snapshot = FaultManager::getSnapshot();
            return balancingRequested.load(std::memory_order_relaxed) &&
                   currentState == RUNNING &&
                   (snapshot.bmsActive | snapshot.bmsLatched |
                    snapshot.hvActive | snapshot.hvLatched) == 0U;
        }

        void performCellBalancing()
        {
            // Already get the values from the settings and convert them to microvolts
            const uint32_t minBalancingVoltage = settings.MIN_BALANCING_VOLTAGE * 1000000U;
            const uint32_t minDiffVoltage = settings.MIN_BALANCING_DIFF_VOLTAGE * 1000000U;

            static bool outputsEnabled = false;
            static bool outputsKnown = false;
            const bool balancingAllowed = isBalancingAllowed();

            // Explicitly disable the drivers once before balancing can start,
            // and whenever any gate becomes false.
            if (!balancingAllowed)
            {
                if (outputsKnown && !outputsEnabled)
                {
                    return;
                }

                bool disabled = true;
                for (auto &slave : mSlaves)
                {
                    disabled = slave.CB_Enable(false) == BCC_STATUS_SUCCESS && disabled;
                }
                outputsEnabled = false;
                outputsKnown = disabled;
                updateFault(CELL_BALANCING_FAULT, !disabled);
                return;
            }

            // Enable the drivers only after every safety gate is true.
            if (!outputsKnown || !outputsEnabled)
            {
                bool enabled = true;
                for (auto &slave : mSlaves)
                {
                    enabled = slave.CB_Enable(true) == BCC_STATUS_SUCCESS && enabled;
                }
                outputsEnabled = enabled;
                outputsKnown = enabled;
                updateFault(CELL_BALANCING_FAULT, !enabled);
                if (!enabled) return;
            }

            // Perform cell balancing if necessary
            bool commandSuccessful = true;
            for (size_t i = 0; i < getNumOfSlaves(); i++)
            {
                for (size_t j = 0; j < mSlaves[i].getCellCount(); j++)
                {
                    bool isAboveMinBalancingVoltage = cellVoltages[i][j] > minBalancingVoltage;
                    bool isAboveMinDiffVoltage = (cellVoltages[i][j] - minCellVoltage) > minDiffVoltage;

                    // Make sure that the cell is above the minimum balancing voltage
                    if (!isAboveMinBalancingVoltage)
                    {
                        continue;
                    }

                    // If the cell is above the minimum difference voltage (w.r.t the lowest cell voltage), start balancing
                    if (isAboveMinDiffVoltage)
                    {
                        commandSuccessful =
                            mSlaves[i].CB_SetIndividualCell(j, true, cBalancingTime) == BCC_STATUS_SUCCESS &&
                            commandSuccessful;
                    }
                }
            }
            updateFault(CELL_BALANCING_FAULT, !commandSuccessful);
        }

        bool diagnostics()
        {
            BCC_Diagnostics::diags_t result;
            bool succes = true;
            // Start measuring the time it takes to perform the diagnostics
            uint32_t startMicros = micros();
            for (auto &slave : mSlaves)
            {
                // If a generic issue arrises during a diagnostic, set succes to false
                if (BCC_Diagnostics::runStartupChecks(&slave, settings.SAFETY_LIMITS, &result) != BCC_STATUS_SUCCESS)
                {
                    succes = false;
                }

                // Check if the result is not 0
                if (*(reinterpret_cast<uint16_t *>(&result)))
                {
                    succes = false;
                    PRINTF_INFO("[SC] Some diagnostics failed on CID %u: %04X\n", slave.getCID(), *(reinterpret_cast<uint16_t *>(&result)));
                    PRINTF_INFO("  ADC1VER: %s\n", result.ADC1VER ? "FAIL" : "OK");
                    PRINTF_INFO("  OVUVVER: %s\n", result.OVUVVER ? "FAIL" : "OK");
                    PRINTF_INFO("  OVUVDET: %s\n", result.OVUVDET ? "FAIL" : "OK");
                    PRINTF_INFO("  CTXOPEN: %s\n", result.CTXOPEN ? "FAIL" : "OK");
                    PRINTF_INFO("  CELLVOLT: %s\n", result.CELLVOLT ? "FAIL" : "OK");
                    PRINTF_INFO("  CONNRES: %s\n", result.CONNRES ? "FAIL" : "OK");
                    PRINTF_INFO("  CTXLEAK: %s\n", result.CTXLEAK ? "FAIL" : "OK");
                    PRINTF_INFO("  CURRMEAS: %s\n", result.CURRMEAS ? "FAIL" : "OK");
                    PRINTF_INFO("  SHUNTNOTCONN: %s\n", result.SHUNTNOTCONN ? "FAIL" : "OK");
                    PRINTF_INFO("  GPIOXOTUT: %s\n", result.GPIOXOTUT ? "FAIL" : "OK");
                    PRINTF_INFO("  GPIOXOPEN: %s\n", result.GPIOXOPEN ? "FAIL" : "OK");
                    PRINTF_INFO("  CBXOPEN: %s\n", result.CBXOPEN ? "FAIL" : "OK");
                }

            }
            double diagTimeMS = (micros() - startMicros) / 1000.0;
            PRINTF_INFO("[SC] Diagnostics took %.2f ms, on average %.2f ms per slave\n", diagTimeMS, diagTimeMS / getNumOfSlaves());
            return succes;
        }

        void handleRegisterRequests()
        {
            RegisterRequest request;
            bool *finishedFlag;
            RegisterReponse *responseTarget;

            taskENTER_CRITICAL();
            if (!registerRequestBusy || registerRequest.cid == 0U)
            {
                taskEXIT_CRITICAL();
                return;
            }

            request = registerRequest;
            finishedFlag = registerRequestFlag;
            responseTarget = registerResponse;
            taskEXIT_CRITICAL();

            PRINTF_INFO("[SC] Processing register request\n");
            RegisterReponse response = {};
            response.status = mSlaves[request.cid - 1U].regRead(request.regAddr, 1, &response.regValue);
            *responseTarget = response;
            *finishedFlag = true;

            taskENTER_CRITICAL();
            registerRequest = {};
            registerRequestFlag = nullptr;
            registerResponse = nullptr;
            registerRequestBusy = false;
            taskEXIT_CRITICAL();
        }

        void runningLoop()
        {
            // PRINTF_ERR("[SC] Running loop in state: %d\n", (currentState));
            // ADC conversions
            const bool conversionSuccessful = startMeasurements();
            const bool measurementsSuccessful = getMeasurements();
            completeMeasurementSetValid = conversionSuccessful && measurementsSuccessful;
            if (completeMeasurementSetValid)
            {
                measurementSequence.fetch_add(1U);
            }
            performCellBalancing();

            // Handle register requests from Companion
            handleRegisterRequests();

            const bool faultStatusSuccessful = faultDetection();

            doCommunicationCheck(faultStatusSuccessful);
        }

        bcc_status_t globalSoftwareReset()
        {
            return BCC::regWriteGlobal(MC33771C_SYS_CFG1_OFFSET,
                                       MC33771C_SYS_CFG1_SOFT_RST(MC33771C_SYS_CFG1_SOFT_RST_ACTIVE_ENUM_VAL));
        }

        /*!
         * @brief This function wakes device(s) up, resets them (if needed), assigns
         * CIDs and checks the communication.
         *
         * @return bcc_status_t Error code.
         */
        bool InitDevices()
        {
            bcc_status_t status;

            /* Wake-up all configured devices (in case they are in SLEEP mode) or
             * move the first device (device closest to MC33664) from IDLE mode to
             * NORMAL mode (in case devices are in IDLE mode). */
            BCC_Communication::wakeUpPattern(mSlaves.size());

            /* Software Reset all configured devices (in case they are already initialized).
             * If the devices are not initialized (CID is equal to 000000b), a write
             * command is sent via communication interface, but the software reset is
             */
            (void)globalSoftwareReset();

            /* Wait for 5 ms - for the IC to be ready for initialization. */
            BCC_MCU_WaitMs(BCC_T_VPWR_READY_MS);

            /* Assign CID to the first node and terminate its RDTX_OUT if only one
             * device is utilised and if loop-back is not required. */

            status = mSlaves[0].assignCid(mSlaves.size());

            if (status != BCC_STATUS_SUCCESS)
            {
                uint8_t &failureCount = cidInitializationFailureCounts[0];
                failureCount++;
                PRINTF_ERR("Failed to assign CID 1 (%u/%u), status code: %lu\n",
                           failureCount,
                           CID_INITIALIZATION_MAX_FAILURES,
                           (uint32_t)status);
                if (failureCount >= CID_INITIALIZATION_MAX_FAILURES)
                {
                    updateFault(CID_INITIALIZATION_FAULT, true);
                }
                return false;
            }
            cidInitializationFailureCounts[0] = 0U;

            /* Init the rest of devices. */
            for (uint8_t i = 1; i < mSlaves.size(); i++)
            {
                BCC_MCU_WaitMs(2U);

                /* Move the following device from IDLE to NORMAL mode (in case the
                 * devices are in IDLE mode).
                 * Note that the WAKE-UP sequence is recognised as two wrong SPI
                 * transfers in devices which are already in the NORMAL mode. That will
                 * increase their COM_STATUS[COM_ERR_COUNT]. */
                BCC_Communication::wakeUpPattern(mSlaves.size());

                status = mSlaves[i].assignCid(mSlaves.size());
                if (status != BCC_STATUS_SUCCESS)
                {
                    uint8_t &failureCount = cidInitializationFailureCounts[i];
                    failureCount++;
                    PRINTF_ERR("Failed to assign CID %u (%u/%u), status code: %lu\n",
                               mSlaves[i].getCID(),
                               failureCount,
                               CID_INITIALIZATION_MAX_FAILURES,
                               (uint32_t)status);
                    if (failureCount >= CID_INITIALIZATION_MAX_FAILURES)
                    {
                        updateFault(CID_INITIALIZATION_FAULT, true);
                    }
                    return false;
                }
                cidInitializationFailureCounts[i] = 0U;
            }

            updateFault(CID_INITIALIZATION_FAULT, false);
            return true;
        }

        /*!
         * @brief This function sets sleep mode to all battery cell controller devices.
         *
         * In case of TPL communication mode, MC33664 has to be put into the sleep mode
         * separately, by the BCC_TPL_Disable function.
         *
         * @return bcc_status_t Error code.
         */
        bcc_status_t Sleep()
        {
            return BCC::regWriteGlobal(MC33771C_SYS_CFG_GLOBAL_OFFSET, MC33771C_SYS_CFG_GLOBAL_GO2SLEEP(MC33771C_SYS_CFG_GLOBAL_GO2SLEEP_ENABLED_ENUM_VAL));
        }

        /**
         * @brief freeRTOS task for SlaveController
         */
        void task(void *argument)
        {
            uint32_t startTick = osKernelGetTickCount(); // Keep track of the time since the task started
            while (true)
            {
				Watchdog::reportBccProgress();

				if (currentState == CRITICAL)
				{
					// Keep platform supervision alive while exposing the critical state.
					osDelay(20U);
					continue;
				}

				const bool initializationRetryDue =
					static_cast<int32_t>(millis() - initializationRetryNotBeforeMs) >= 0;

                if (currentState == DEVICE_INITIALIZATION && initializationRetryDue)
                {
                    // 1. TPL plus daisy chain / CID initialization.
                    const bool tplReady = BCC_Communication::TPL_Enable() == BCC_STATUS_SUCCESS;
                    if (!tplReady)
                    {
                        ++tplInitializationFailureCount;
                        if (tplInitializationFailureCount >= CID_INITIALIZATION_MAX_FAILURES)
                        {
                            updateFault(TPL_FAULT, true);
                        }
						initializationRetryNotBeforeMs = millis() + 2000U;
                    }
                    else
                    {
                        tplInitializationFailureCount = 0U;
                        updateFault(TPL_FAULT, false);
                        if (!InitDevices())
                        {
                            // Retry the complete chain after a short delay. A
                            // source only becomes an ERROR once its own retry
                            // budget is exhausted in InitDevices().
							initializationRetryNotBeforeMs = millis() + 2000U;
                        }
                        else
                        {
                            setState(REGISTER_INITIALIZATION);
                        }
                    }
                }

                if (currentState == REGISTER_INITIALIZATION && initializationRetryDue)
                {

                    // 2. Register initialization
                    if (!initializeRegisters())
                    {
                        ++registerInitializationFailureCount;
                        if (registerInitializationFailureCount >= CID_INITIALIZATION_MAX_FAILURES)
                        {
                            updateFault(REGISTER_INITIALIZATION_FAULT, true);
                        }
						initializationRetryNotBeforeMs = millis() + 2000U;
                    }
                    else
                    {
                        registerInitializationFailureCount = 0U;
                        updateFault(REGISTER_INITIALIZATION_FAULT, false);
                        setState(PERFORMING_DIAGNOSTICS);
                    }
                }

                if (currentState == PERFORMING_DIAGNOSTICS && initializationRetryDue)
                {
                    // 5. Diagnostics
                    bool diagnosticsSuccessful = true;
                    if (BMS_RUN_STARTUP_DIAGNOSTICS != 0U)
                    {
                        diagnosticsSuccessful = diagnostics();
                    }
                    else
                    {
                        PRINTF_ERR("[SC] WARNING: startup diagnostics are disabled for bench testing\n");
                    }
                    if (!diagnosticsSuccessful)
                    {
                        ++diagnosticsFailureCount;
                        if (diagnosticsFailureCount >= CID_INITIALIZATION_MAX_FAILURES)
                        {
                            updateFault(DIAGNOSTICS_FAULT, true);
                        }
                        else
                        {
                            updateFault(DIAGNOSTICS_FAULT, false);
                        }
                        setState(PERFORMING_DIAGNOSTICS);
						initializationRetryNotBeforeMs = millis() + 2000U;
                    }
                    else
                    {
                        diagnosticsFailureCount = 0U;
                        updateFault(DIAGNOSTICS_FAULT, false);
                        const bool conversionSuccessful = startMeasurements();
                        const bool measurementsSuccessful = conversionSuccessful && getMeasurements(true);
                        completeMeasurementSetValid = conversionSuccessful && measurementsSuccessful;

                        bool faultStatusSuccessful = false;
                        if (measurementsSuccessful)
                        {
                            faultStatusSuccessful = faultDetection();
                        }
                        doCommunicationCheck(faultStatusSuccessful);

                        setState(RUNNING);
                        FaultManager::setStartupComplete(true);
                    }
                }

                if (currentState == RUNNING)
                {
                    runningLoop();
                }

                if (currentState == CRITICAL)
                {
                    // Configuration is unrecoverable by current software.
                }

                // Schedule the next loop iteration BMS_MAIN_LOOP_PERIOD ms after this one to achieve constant frequency
                osDelayUntil(startTick += settings.BMS_MAIN_LOOP_PERIOD / portTICK_PERIOD_MS);
            }
        }

        /**
         * @brief Load the configuration from the UserSettings.h file
         * @return true if the configuration is valid, false otherwise
         */
        bool loadConfig()
        {
            // For now always use the default settings. Later on we can add a way to load settings from flash
            settings = DEFAULT_SETTINGS;

            // Verify that the set maximum current is measurable with the given shunt resistance
            double maxCurrentForShunt = 0.15 / settings.SHUNT_RESISTANCE;
            if (settings.SAFETY_LIMITS.DISCHARGE_CURRENT_LIMIT > maxCurrentForShunt || settings.SAFETY_LIMITS.CHARGE_CURRENT_LIMIT > maxCurrentForShunt)
            {
                PRINTF_ERR("[SC] CONFIG ERR: Current limits are not measurable with the current shunt resistance!\n");
                return false;
            }

            // Verify that the main loop period is achievable
            // If the conversion time is longer than the main loop period, we know for sure that we can't achieve the desired frequency
            const double safetyFactor = 2; // We want to have some margin
            double minConversionTime = (BCC_T_EOC_TIMEOUT_US << BCC_ADC_AVG) / 1000.0;
            minConversionTime += 3.0; // Add 3ms for the settling time of the LP filters
            const double maxBalancingFactor = 1 - (minConversionTime / settings.BMS_MAIN_LOOP_PERIOD);
            if (settings.BMS_MAIN_LOOP_PERIOD < minConversionTime * safetyFactor)
            {
                const double timeOvershootFactor = minConversionTime / (settings.BMS_MAIN_LOOP_PERIOD / safetyFactor);
                const int32_t maxAllowedAveraging = 1 << (int32_t)(static_cast<int32_t>(BCC_ADC_AVG) - ceil(log2(timeOvershootFactor)));
                PRINTF_ERR("[SC] CONFIG ERR: Main loop period is too short for the given averaging and safety margin!\n");
                PRINTF_ERR("    Main loop period: %lu ms\n", settings.BMS_MAIN_LOOP_PERIOD);
                PRINTF_ERR("    Min conversion time: %.2f ms\n", minConversionTime);
                PRINTF_ERR("    Maximum allowed averaging: %ld\n", maxAllowedAveraging);
                return false;
            }

            // Calculate the balancing time based on the capacity of the battery, the balancing resistance and the max balancing factor
            // Print important information about the config
            // Only do this if IMPROVED_BALANCING_ACCURACY is disabled
            if (!settings.IMPROVED_BALANCING_ACCURACY)
            {
                const double batteryDrainedMinutes = (settings.BATTERY_AMPHOURS / (settings.SAFETY_LIMITS.OVERVOLTAGE_LIMIT / BMS_BAL_RESISTANCE)) * 60.0;
                cBalancingTime = lround((batteryDrainedMinutes / maxBalancingFactor) * 0.25e-2);
                PRINTF_WARN("[SC] NOTE: The automagically configured balancing time is %u minutes per trigger\n", cBalancingTime);
            }
            else
            {
                cBalancingTime = 0;
                PRINTF_WARN("[SC] NOTE: Improved balancing accuracy is enabled, using a balancing time of 30s per trigger\n");
            }

            PRINTF_WARN("[SC] NOTE: Current config allows for balancing a maximum of %.1f%% of the time \n", maxBalancingFactor * 100);

            // Create BCC objects and assign CID's starting from 1
            for (size_t i = 0; i < settings.SLAVE_CONFIG.size(); i++)
            {
                mSlaves.emplace_back(settings.SLAVE_CONFIG[i], i + 1);

                // If the slave is responsible for the current measurement, save the CID
                // For now we only support one slave for current measurement
                if (mSlaves.back().currentSenseEnabled())
                {
                    currentMeasurementSlaveIdx = i;
                }

                // Check config of slave last pushed to the array
                if (!mSlaves.back().hasValidConfig())
                {
                    return false;
                }
            }
            cidInitializationFailureCounts.assign(mSlaves.size(), 0U);
            return true;
        }
    }

    /*******************************************************************************
     * Public functions
     ******************************************************************************/
    void setup(CAN *can)
    {
        // Make sure to start at correct state. This also faults the relay driver to be off during startup
        setState(DEVICE_INITIALIZATION);
        FaultManager::setStartupComplete(false);

        BCC_MCU_Assert(can != nullptr);
        mCAN = can;

        if (!loadConfig())
        {
            updateFault(INVALID_CONFIG, true);
            FaultManager::enterCritical();
            setState(CRITICAL);
			bmsTaskHandle = osThreadNew(task, NULL, &bmsTask_attributes);
            return;
        }

        FaultManager::setWarning(FaultManager::Warning::StartupDiagnosticsBypassed,
                                 BMS_RUN_STARTUP_DIAGNOSTICS == 0U);

        BCC_Communication::setup(settings.TPL_TX_TIMEOUT_MS, settings.TPL_RX_TIMEOUT_MS);

        // Enable backup domain register access
        HAL_PWR_EnableBkUpAccess();

        // Make some memory available for the vectors and arrays
        cellVoltages.resize(getNumOfSlaves());
        NTCtemperatures.resize(getNumOfSlaves());
        ICtemperatures.resize(getNumOfSlaves());
        for (size_t i = 0; i < getNumOfSlaves(); i++)
        {
            cellVoltages[i].resize(mSlaves[i].getCellCount());
            NTCtemperatures[i].resize(mSlaves[i].getNTCCount());
            ICtemperatures[i] = 0;
        }

        // Create task 'n stuff
        bmsTaskHandle = osThreadNew(task, NULL, &bmsTask_attributes);
    }

    bool isNewDataAvailable(uint32_t &lastSeenMeasurement)
    {
        const uint32_t currentMeasurement = measurementSequence.load();
        if (currentMeasurement != lastSeenMeasurement)
        {
            lastSeenMeasurement = currentMeasurement;
            return true;
        }
        return false;
    }

    bool areMeasurementsFresh()
    {
        return measurementsAreFresh();
    }

    BMSState getState()
    {
        return currentState;
    }

    size_t getNumOfSlaves()
    {
        return settings.SLAVE_CONFIG.size();
    }

    size_t getCellCount()
    {
        size_t sum = 0;
        for (auto &slave : mSlaves)
        {
            sum += slave.getCellCount();
        }
        return sum;
    }

    vector<size_t> getCellCountPerSlave()
    {
        vector<size_t> cellCounts;
        cellCounts.reserve(getNumOfSlaves());
        for (auto &slave : mSlaves)
        {
            cellCounts.push_back(slave.getCellCount());
        }
        return cellCounts;
    }

    vector<size_t> getNTCCountPerSlave()
    {
        vector<size_t> NTCCounts;
        NTCCounts.reserve(getNumOfSlaves());
        for (auto &slave : mSlaves)
        {
            NTCCounts.push_back(slave.getNTCCount());
        }
        return NTCCounts;
    }

    const vector<vector<uint32_t>> &getCellVoltages()
    {
        return cellVoltages;
    }

    const std::vector<std::vector<bool>> getBalancingList()
    {
        vector<vector<bool>> balanceActive;
        balanceActive.reserve(getNumOfSlaves());
        for (auto &slave : mSlaves)
        {
            balanceActive.push_back(slave.getBalancingList());
        }
        return balanceActive;
    }

    uint16_t getBalancingMask(size_t slaveIndex)
    {
        if (slaveIndex >= mSlaves.size())
        {
            return 0U;
        }

        uint16_t mask = 0U;
        for (uint8_t cellIndex = 0U; cellIndex < 12U; ++cellIndex)
        {
            if (mSlaves[slaveIndex].isCellBalancing(cellIndex))
            {
                mask |= static_cast<uint16_t>(1U << cellIndex);
            }
        }
        return mask;
    }

    uint32_t getMinCellVoltage()
    {
        return minCellVoltage;
    }

    uint32_t getMaxCellVoltage()
    {
        return maxCellVoltage;
    }

    uint32_t getPackVoltage()
    {
        return packVoltage;
    }

    double getCurrent()
    {
        return packCurrent;
    }

    bool isHVReady()
    {
        return currentState == RUNNING &&
               FaultManager::canEnableHv() &&
               measurementsAreFresh();
    }

    bool isChargingAllowed()
    {
        // Charging is always allowed if there are no faults and we are in the running state (RELAY closed)
        return currentState == RUNNING &&
               FaultManager::canEnableHv() &&
               measurementsAreFresh();
    }

    void setBalancingRequest(bool requested)
    {
        balancingRequested.store(requested, std::memory_order_relaxed);
    }

    bool isBalancingRequested()
    {
        return balancingRequested.load(std::memory_order_relaxed);
    }

    const vector<vector<uint16_t>> &getNTCtemps()
    {
        return NTCtemperatures;
    }

    uint16_t getMinNTCtemp()
    {
        return minNTCtemperature;
    }

    uint16_t getMaxNTCtemp()
    {
        return maxNTCtemperature;
    }

    const vector<uint16_t> &getICtemps()
    {
        return ICtemperatures;
    }

    uint16_t getMinICtemp()
    {
        return minICtemperature;
    }

    uint16_t getMaxICtemp()
    {
        return maxICtemperature;
    }

    uint16_t getSoC()
    {
        double current;
        double ampHour;
        bcc_status_t status;
        if ((status = mSlaves[currentMeasurementSlaveIdx].meas_GetAmpHourAndIAvg(settings.SHUNT_RESISTANCE, settings.INVERT_CURRENT, &ampHour, &current)) != BCC_STATUS_SUCCESS)
        {
            // In case of an error, return 0%
            return BCC_AMPHOUR_TO_SOC(0, settings.BATTERY_AMPHOURS);
        }
        return BCC_AMPHOUR_TO_SOC(ampHour, settings.BATTERY_AMPHOURS);
    }

    void setSoC(uint16_t soc)
    {
        double ampHour = BCC_SOC_TO_AMPHOUR(soc, settings.BATTERY_AMPHOURS);
        mSlaves[currentMeasurementSlaveIdx].setAhCounter(ampHour);
    }

    bool requestRegister(RegisterRequest requestInfo, bool *flag, RegisterReponse *regResponse)
    {
        if (requestInfo.cid == 0U || requestInfo.cid > mSlaves.size() || flag == nullptr || regResponse == nullptr)
        {
            return false;
        }

        taskENTER_CRITICAL();
        if (registerRequestBusy)
        {
            taskEXIT_CRITICAL();
            return false;
        }

        registerRequest = requestInfo;
        registerRequestFlag = flag;
        registerResponse = regResponse;
        registerRequestBusy = true;
        taskEXIT_CRITICAL();
        return true;
    }

}

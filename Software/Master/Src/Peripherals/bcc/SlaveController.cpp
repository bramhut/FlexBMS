
/*!
 * @file slaveController.cpp
 *
 * Controller for the slave devices in the TPL chain.
 */

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "TimeFunctions.h"
#include "bcc/SlaveController.h"
#include "bcc/bcc_diagnostics.h"
#include "bcc/UserSettings.h"
#include "BoardIO.h"
#include "USBCOM.h"
#include "pcc.h"

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

        uint16_t mFaults = 0;      // Current faults active
        bool mClearFaults = false; // Clear all faults, if allowed

        BMSState currentState = DEVICE_INITIALIZATION; // Current state of the BMS

        static vector<BCC> mSlaves; /* Array of BCC devices */

        UserSettings_t settings;

        uint16_t cBalancingTime = 0; // Time in [min] for a single cell balancing operation

        uint8_t currentMeasurementSlaveIdx = 0; // Index of the slave responsible for current measurement

        bool newDataAvailable = false; // Set to true when new data is available to be read

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

        RegisterRequest registerRequest = {};
        bool *registerRequestFlag = nullptr;
        RegisterReponse *registerResponse = nullptr;

        uint32_t timeLastFaultEnabled = 0; // The time when the last fault has been enabled in [milliSeconds]

        /*******************************************************************************
         * Private functions
         ******************************************************************************/

        void handleRelay()
        {
            
            bool relayState = currentState == RUNNING &&
                              mFaults == 0 &&
                              PCC::getPCCState() != PCC::PCC_STATE::STARTUP &&
                              PCC::getPCCState() != PCC::PCC_STATE::ERROR;
            
            IO::setRelay(relayState);

        }

        void setState(BMSState state)
        {
            currentState = state;
            handleRelay();
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
            case PANIC:
                PRINTF_INFO("[SC] New state: PANIC\n");
                break;
            }
        }

        bool isFaultEnabled(BMSFault fault)
        {
            return mFaults & (0x01 << fault);
        }

        void setFault(BMSFault fault, bool enable)
        {
            uint16_t lastFaults = mFaults;
            if (enable)
            {
                // Enable fault
                mFaults |= (0x01 << fault);
                timeLastFaultEnabled = millis();
            }
            else if (millis() - timeLastFaultEnabled > settings.MINIMUM_FAULT_ACTIVE_TIME)
            {
                // Check if companion has requested to clear all faults
                // if (mClearFaults)
                // {
                //     mFaults = 0;
                //     mClearFaults = false;
                // }

                // Check if diagnostic fault is enabled.
                // Diagnostic faults should never be cleared.
                if (!isFaultEnabled(DIAGNOSTICS_FAULT)) {
                    
                    mFaults = 0;
                }
            }

            // If the fault state changed, handle the relay
            if (lastFaults != mFaults)
            {
                PRINTF_INFO("[SC] Fault state changed, %s fault %u: %04X\n", enable ? "enabled" : "disabled", fault, mFaults);
                handleRelay();
            }
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

        void startMeasurements()
        {
            // Pause CB for all slaves
            for (auto &slave : mSlaves)
            {
                slave.CB_Pause(true);
            }

            // It is recommended to wait for at least 3ms before starting the ADC conversion
            // to ensure that the LP filters have settled.
            delay(3);

            BCC::meas_StartConversionGlobal();

            // Wait for the conversion to finish
            for (auto &slave : mSlaves)
            {
                if (slave.meas_WaitOnConversion(BCC_ADC_AVG) != BCC_STATUS_SUCCESS)
                {
                    PRINTF_WARN("[SC] Slave (CID: %u) failed on: wait on conversion\n", slave.getCID());
                    continue;
                }
            }

            // Resume CB
            for (auto &slave : mSlaves)
            {
                slave.CB_Pause(false);
            }
        };

        void faultDetection()
        {
            uint16_t combinedFaults[BCC_STAT_CNT] = {0};
            double iAvg;
            double ampHour;
            for (auto &slave : mSlaves)
            {
                slave.fault_GetStatus(combinedFaults);

                // if (slave.currentSenseEnabled())
                // {
                //     slave.meas_GetAmpHourAndIAvg(settings.SHUNT_RESISTANCE, settings.INVERT_CURRENT, &ampHour, &iAvg);
                //     setFault(OVERCURRENT_LIMIT, iAvg < -settings.SAFETY_LIMITS.DISCHARGE_CURRENT_LIMIT || iAvg > settings.SAFETY_LIMITS.CHARGE_CURRENT_LIMIT);
                // }

                // TODO Log all fault registers in FLASH or something
            }

            double current = IO::getCurrent();
            setFault(OVERCURRENT_LIMIT,  current < -settings.SAFETY_LIMITS.DISCHARGE_CURRENT_LIMIT || current > settings.SAFETY_LIMITS.CHARGE_CURRENT_LIMIT);

            setFault(OVERVOLTAGE_LIMIT, combinedFaults[BCC_FS_CELL_OV] != 0);
            setFault(UNDERVOLTAGE_LIMIT, combinedFaults[BCC_FS_CELL_UV] != 0);
            setFault(TEMPERATURE_LIMIT, combinedFaults[BCC_FS_AN_OT_UT] != 0);
            setFault(OPEN_SHORT_FAULT, combinedFaults[BCC_FS_CB_OPEN] != 0 || combinedFaults[BCC_FS_CB_SHORT] != 0 || combinedFaults[BCC_FS_GPIO_SHORT]);
            setFault(IC_TEMPERATURE, (combinedFaults[BCC_FS_FAULT2] & MC33771C_FAULT2_STATUS_IC_TSD_FLT_MASK) != 0);
            setFault(SYSTEM_FAULT, combinedFaults[BCC_FS_FAULT1] != 0 || combinedFaults[BCC_FS_FAULT2] != 0 || combinedFaults[BCC_FS_FAULT3] != 0);
        };

        void getMeasurements()
        {
            bcc_status_t status;
            static uint32_t loopCount = 0;
            // Only fetch measurements every x loops
            if ((loopCount++ % settings.BMS_MEASUREMENT_PERIOD_FACTOR) != 0)
            {
                return;
            }

            // PRINTF_INFO("[SC] Fetching measurements\n");
            for (auto &slave : mSlaves)
            {
                // Fetch the measurements from the BCC
                if ((status = slave.meas_GetRawValues()) != BCC_STATUS_SUCCESS)
                {
                    PRINTF_WARN("[SC] Slave (CID: %u) failed on: get raw values\n", slave.getCID());
                    continue;
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
                    continue;
                }

                // Get the NTC temperatures
                if (mSlaves[i].meas_GetNTCTemperatures(NTCtemperatures[i], settings.NTC_RESISTANCE, settings.NTC_BETA) != BCC_STATUS_SUCCESS)
                {
                    PRINTF_WARN("[SC] Slave (CID: %u) failed on: get NTC temperatures\n", mSlaves[i].getCID());
                    continue;
                }

                // Also get the IC temperature
                if (mSlaves[i].meas_GetIcTemperature(&ICtemperatures[i]) != BCC_STATUS_SUCCESS)
                {
                    PRINTF_WARN("[SC] Slave (CID: %u) failed on: get IC temperature\n", mSlaves[i].getCID());
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
            if (settings.AUTO_CALIBRATE_SOC)
            {
                // If the pack voltage is above the threshold, set the SoC to 100%
                const uint32_t calibrateVoltage = settings.AUTO_CALIBRATE_SOC_THRESHOLD * settings.SAFETY_LIMITS.OVERVOLTAGE_LIMIT * getCellCount() * 1'000'000U;
                if (getPackVoltage() > calibrateVoltage)
                {
                    setSoC(BCC_SOC_TO_SOCRAW(1));
                }
            }

            // Set the newDataAvailable flag to true
            newDataAvailable = true;
        }

        void doCommunicationCheck()
        {
            for (auto &slave : mSlaves)
            {
                // Check if the BCC has a communication timeout
                if ((millis() - slave.getTimeReceivedLastMeasurement()) > settings.SAFETY_LIMITS.COMMUNICATION_TIMEOUT)
                {
                    // If there is a communication timeout, set the fault and return
                    setFault(COMMUNICATION_TIMEOUT, true);
                    return;
                }
            }
            // Clear the fault if no BCC has a communication timeout
            setFault(COMMUNICATION_TIMEOUT, false);

            // CID presence check
            if (!allCIDsPresence())
            {
                // We lost communication with one of our slaves, go back to DEVICE_INITIALIZATION
                setState(DEVICE_INITIALIZATION);
                return;
            }
        }

        // Checks based on the current active faults if cell balancing is allowed
        bool isBalancingAllowed()
        {
            return ((mFaults & ~(1U << (uint16_t)OVERVOLTAGE_LIMIT)) & ~(1U << (uint16_t)SOC_LIMIT)) == 0;
        }

        void performCellBalancing()
        {
            // Already get the values from the settings and convert them to microvolts
            const uint32_t minBalancingVoltage = settings.MIN_BALANCING_VOLTAGE * 1000000U;
            const uint32_t minDiffVoltage = settings.MIN_BALANCING_DIFF_VOLTAGE * 1000000U;

            static bool prevBalancingAllowed = false;
            bool balancingAllowed = isBalancingAllowed();

            // If cell balancing is not allowed anymore, disable CB_DRVEN bit (stops balancing for all cells)
            if (prevBalancingAllowed && !balancingAllowed)
            {
                for (auto &slave : mSlaves)
                {
                    slave.CB_Enable(false);
                }
                prevBalancingAllowed = false;
            }

            // If cell balancing was not allowed, but is now allowed, enable CB_DRVEN bit (allows balancing for all cells)
            if (!prevBalancingAllowed && balancingAllowed)
            {
                for (auto &slave : mSlaves)
                {
                    slave.CB_Enable(true);
                }
                prevBalancingAllowed = true;
            }

            // No reason to continue if balancing is not allowed
            if (!balancingAllowed)
            {
                return;
            }

            // Perform cell balancing if necessary
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
                        mSlaves[i].CB_SetIndividualCell(j, true, cBalancingTime);
                    }
                }
            }
        }

        bool diagnostics()
        {
            BCC_Diagnostics::diags_t result;
            bool succes = true;
            CAN::Frame frame;
            frame.id = settings.CAN_BCC_DIAG_ID;
            frame.length = 3;

            // Start measuring the time it takes to perform the diagnostics
            uint32_t startMicros = micros();
            for (auto &slave : mSlaves)
            {
                frame.data[0] = slave.getCID();

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

                memcpy(frame.data + 1, &result, 2);
                mCAN->sendMessage(frame);
            }
            double diagTimeMS = (micros() - startMicros) / 1000.0;
            PRINTF_INFO("[SC] Diagnostics took %.2f ms, on average %.2f ms per slave\n", diagTimeMS, diagTimeMS / getNumOfSlaves());
            return succes;
        }

        void handleRegisterRequests()
        {
            RegisterReponse response;
            if (registerRequest.cid != 0)
            {
                PRINTF_INFO("[SC] Processing register request\n");
                response.status = mSlaves[registerRequest.cid - 1].regRead(registerRequest.regAddr, 1, &response.regValue);
                *registerRequestFlag = true;
                *registerResponse = response;
                registerRequest.cid = 0;
            }
        }

        void runningLoop()
        {
            // PRINTF_ERR("[SC] Running loop in state: %d\n", (currentState));
            // ADC conversions
            startMeasurements();

            getMeasurements();

            // Cell balancing
            // performCellBalancing();

            // Handle register requests from Companion
            handleRegisterRequests();

            faultDetection();

            doCommunicationCheck();
            handleRelay();
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
                PRINTF_ERR("Failed to assign CID to the first device, status code: %lu\n", (uint32_t)status);
                return false;
            }

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
                    return false;
                }
            }

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

        void handleCANMessages()
        {
            static uint32_t loopCount = 0;
            CAN::Frame frame;

            // Check if it is time to send the measurements
            // Only send measurements if the device is in the RUNNING state, otherwise data will be bad
            if ((loopCount % settings.CAN_MEASUREMENT_MSG_PERIOD_FACTOR) == 0 && currentState == RUNNING)
            {
                // Send Measurements_1 message
                frame.id = settings.CAN_MEAS1_ID;
                frame.length = 8;
                frame.data16[0] = getMinCellVoltage() / 100U;  // Min cell voltage [0.1mV]
                frame.data16[1] = getMaxCellVoltage() / 100U;  // Max cell voltage [0.1mV]
                frame.data16[2] = getPackVoltage() / 100'000U; // Pack voltage [100mV]
                frame.data16[3] = getSoC();                    // State of Charge [raw]
                mCAN->sendMessage(frame);

                // Send Measurements_2 message
                frame.id = settings.CAN_MEAS2_ID;
                frame.length = 8;
                double halEffectCurrent = IO::getCurrent();
                frame.data_s16[0] = BCC_CURRENT_TO_RAW(halEffectCurrent);  // Current [1/64 A]
                frame.data16[1] = getMinNTCtemp(); // Min NTC temperature [raw]
                frame.data16[2] = getMaxNTCtemp(); // Max NTC temperature [raw]
                frame.data16[3] = getMaxICtemp();  // Max IC temperature [raw]
                mCAN->sendMessage(frame);
            }

            // Check if it is time to send the BMS State message
            if ((loopCount % settings.CAN_BMS_STATE_MSG_PERIOD_FACTOR) == 0)
            {
                // Send BMS State message
                frame.id = settings.CAN_BMS_STATE_ID;
                frame.length = 8;
                frame.data64 = 0;                         // Clear the data
                frame.data[0] = getState();               // Byte 0
                memcpy(frame.data + 1, &mFaults, 2);      // Byte 1-2
                frame.data[3] = BCC::getCANstateGlobal(); // (Part of) Byte 3
                for (size_t i = 1; i < 20; i++)           // Bytes 3-7
                {
                    if (i > mSlaves.size())
                    {
                        break;
                    }
                    frame.data[3 + i / 4] <<= 2;
                    frame.data[3 + i / 4] |= mSlaves[i - 1].getCANstate();
                }
                mCAN->sendMessage(frame);
            }

            loopCount++;
        }

        /**
         * @brief freeRTOS task for SlaveController
         */
        void task(void *argument)
        {
            uint32_t startTick = osKernelGetTickCount(); // Keep track of the time since the task started
            while (true)
            {
                if (currentState == DEVICE_INITIALIZATION)
                {
                    // 1. Daisy chain / CID initialization
                    if (!InitDevices())
                    {
                        setFault(CID_INITIALIZATION_FAULT, true);

                        // Don't change the state here to make sure we keep trying to initialize devices
                        // Delay a little bit so we don't spam it
                        delay(2000);
                    }
                    else
                    {
                        setFault(CID_INITIALIZATION_FAULT, false);
                        setState(REGISTER_INITIALIZATION);
                    }
                }

                if (currentState == REGISTER_INITIALIZATION)
                {

                    // 2. Register initialization
                    if (!initializeRegisters())
                    {
                        setFault(REGISTER_INITIALIZATION_FAULT, true);
                        setState(PANIC);
                    }
                    else
                    {
                        setState(PERFORMING_DIAGNOSTICS);
                    }
                }

                if (currentState == PERFORMING_DIAGNOSTICS)
                {
                    // 5. Diagnostics
                    if (!diagnostics())
                    {
                        setFault(DIAGNOSTICS_FAULT, true);
                    }
                    setState(RUNNING);
                }

                if (currentState == RUNNING)
                {
                    runningLoop();
                }

                if (currentState == PANIC)
                {
                    // Don't do anything I guess
                }

                handleCANMessages();

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

        BCC_MCU_Assert(can != nullptr);
        mCAN = can;

        if (!loadConfig())
        {
            setFault(INVALID_CONFIG, true);
            setState(PANIC);
        }

        BCC_Communication::setup();

        if (BCC_Communication::TPL_Enable() != BCC_STATUS_SUCCESS)
        {
            // TODO this: reportBCCError(status, 0);
            setFault(TPL_FAULT, true);
            setState(PANIC);
        }

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

    bool isNewDataAvailable()
    {
        if (newDataAvailable)
        {
            newDataAvailable = false;
            return true;
        }
        return false;
    }

    void clearFaults()
    {
        mClearFaults = true;
    }

    uint16_t getFaults()
    {
        return mFaults;
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
        vector<size_t> cellCounts(getNumOfSlaves());
        for (auto &slave : mSlaves)
        {
            cellCounts.push_back(slave.getCellCount());
        }
        return cellCounts;
    }

    vector<size_t> getNTCCountPerSlave()
    {
        vector<size_t> NTCCounts(getNumOfSlaves());
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

    // int16_t getCurrent()
    // {
    //     double current;
    //     double ampHour;
    //     bcc_status_t status;
    //     if ((status = mSlaves[currentMeasurementSlaveIdx].meas_GetAmpHourAndIAvg(settings.SHUNT_RESISTANCE, settings.INVERT_CURRENT, &ampHour, &current)) != BCC_STATUS_SUCCESS)
    //     {
    //         // In case of an error, return 0
    //         return BCC_CURRENT_TO_RAW(0);
    //     }
    //     return BCC_CURRENT_TO_RAW(current);
    // }

    bool isChargingAllowed()
    {
        // Charging is always allowed if there are no faults and we are in the running state (RELAY closed)
        return currentState == RUNNING && mFaults == 0;
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

    void requestRegister(RegisterRequest requestInfo, bool *flag, RegisterReponse *regResponse)
    {
        registerRequest = requestInfo;
        registerRequestFlag = flag;
        registerResponse = regResponse;
    }

}
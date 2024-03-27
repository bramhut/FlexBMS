#include "bcc/SlaveController.h"

/*!
 * @file slaveController.cpp
 *
 * Controller for the slave devices in the TPL chain.
 */

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "Time.h"
#include "bcc/bcc_utils.h"
#include "bcc/bcc_communication.h"
#include "bcc/MC33771C.h"
#include <vector>
#include "bcc/UserSettings.h"

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
            .stack_size = configMINIMAL_STACK_SIZE * 4,
            .priority = (osPriority_t)osPriorityNormal,
        };

        /*******************************************************************************
         * Private variables
         ******************************************************************************/

        BMSFault mCurrentFault = NO_FAULT; /* Current fault state */

        std::vector<BCC> mSlaves; /* Array of BCC devices */

        bool loopbackMode;

        double shuntResistance; // [Ohm]

        SafetyLimits_t safetyLimits;

        /*******************************************************************************
         * Private functions
         ******************************************************************************/
        
        /**
         * @brief Convert temperature to millivolts
        */
        uint16_t convertTempToVoltage(double temp)
        {
            // TODO implement this
            return 2000U;
        }

        std::vector<bcc_init_reg_t> getInitGlobalRegisterMapping()
        {

            uint16_t ov_limit_mv = (unsigned int) safetyLimits.OVERVOLTAGE_LIMIT * 1000;
            uint16_t uv_limit_mv = (unsigned int) safetyLimits.UNDERVOLTAGE_LIMIT * 1000;
            
            uint16_t ot_limit_mv = convertTempToVoltage(safetyLimits.OVERTEMPERATURE_LIMIT);
            uint16_t ut_limit_mv = convertTempToVoltage(safetyLimits.UNDERTEMPERATURE_LIMIT);

            return {
                {MC33771C_GPIO_CFG1_OFFSET, MC33771C_GPIO_CFG1_POR_VAL, MC33771C_GPIO_CFG1_VALUE},
                {MC33771C_GPIO_CFG2_OFFSET, MC33771C_GPIO_CFG2_POR_VAL, MC33771C_GPIO_CFG2_VALUE},
                {MC33771C_TH_ALL_CT_OFFSET, MC33771C_TH_ALL_CT_POR_VAL, (uint16_t) MC33771C_TH_ALL_CT_VALUE(ov_limit_mv, uv_limit_mv)},
                {MC33771C_TH_AN6_OT_OFFSET, MC33771C_TH_AN6_OT_POR_VAL, (uint16_t) MC33771C_TH_ANX_OT_VALUE(ot_limit_mv)},
                {MC33771C_TH_AN5_OT_OFFSET, MC33771C_TH_AN5_OT_POR_VAL, (uint16_t) MC33771C_TH_ANX_OT_VALUE(ot_limit_mv)},
                {MC33771C_TH_AN4_OT_OFFSET, MC33771C_TH_AN4_OT_POR_VAL, (uint16_t) MC33771C_TH_ANX_OT_VALUE(ot_limit_mv)},
                {MC33771C_TH_AN3_OT_OFFSET, MC33771C_TH_AN3_OT_POR_VAL, (uint16_t) MC33771C_TH_ANX_OT_VALUE(ot_limit_mv)},
                {MC33771C_TH_AN2_OT_OFFSET, MC33771C_TH_AN2_OT_POR_VAL, (uint16_t) MC33771C_TH_ANX_OT_VALUE(ot_limit_mv)},
                {MC33771C_TH_AN1_OT_OFFSET, MC33771C_TH_AN1_OT_POR_VAL, (uint16_t) MC33771C_TH_ANX_OT_VALUE(ot_limit_mv)},
                {MC33771C_TH_AN0_OT_OFFSET, MC33771C_TH_AN0_OT_POR_VAL, (uint16_t) MC33771C_TH_ANX_OT_VALUE(ot_limit_mv)},
                {MC33771C_TH_AN6_UT_OFFSET, MC33771C_TH_AN6_UT_POR_VAL, (uint16_t) MC33771C_TH_ANX_UT_VALUE(ut_limit_mv)},
                {MC33771C_TH_AN5_UT_OFFSET, MC33771C_TH_AN5_UT_POR_VAL, (uint16_t) MC33771C_TH_ANX_UT_VALUE(ut_limit_mv)},
                {MC33771C_TH_AN4_UT_OFFSET, MC33771C_TH_AN4_UT_POR_VAL, (uint16_t) MC33771C_TH_ANX_UT_VALUE(ut_limit_mv)},
                {MC33771C_TH_AN3_UT_OFFSET, MC33771C_TH_AN3_UT_POR_VAL, (uint16_t) MC33771C_TH_ANX_UT_VALUE(ut_limit_mv)},
                {MC33771C_TH_AN2_UT_OFFSET, MC33771C_TH_AN2_UT_POR_VAL, (uint16_t) MC33771C_TH_ANX_UT_VALUE(ut_limit_mv)},
                {MC33771C_TH_AN1_UT_OFFSET, MC33771C_TH_AN1_UT_POR_VAL, (uint16_t) MC33771C_TH_ANX_UT_VALUE(ut_limit_mv)},
                {MC33771C_TH_AN0_UT_OFFSET, MC33771C_TH_AN0_UT_POR_VAL, (uint16_t) MC33771C_TH_ANX_UT_VALUE(ut_limit_mv)},
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

            std::vector<bcc_init_reg_t> globalRegisters = getInitGlobalRegisterMapping();

            // Write global registers
            for (uint16_t i; i < globalRegisters.size(); i++)
            {
                if (globalRegisters[i].value != globalRegisters[i].defaultVal)
                {
                    if (BCC_Communication::regWriteGlobal(globalRegisters[i].address, globalRegisters[i].value) != BCC_STATUS_SUCCESS)
                    {
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
                    if (BCC_Communication::regWrite(slave.getCID(), MC33771C_SYS_CFG1_OFFSET, MC33771C_SYS_CFG1_VALUE(true)) != BCC_STATUS_SUCCESS) {
                        return false;
                    }
                }

                // Depending on number of NTCs used, set unused GPIOs to digital in, (and highest bits to 0) 
                uint16_t regValue = (0x2AAA << 2 * slave.getNTCCount()) & 0x3FFF;
                if (BCC_Communication::regWrite(slave.getCID(), MC33771C_GPIO_CFG1_OFFSET, regValue) != BCC_STATUS_SUCCESS) {
                    return false;
                }

                // Disable OV/UV detection for unused cells  (reg OV_UV_EN)
                uint8_t maxCellCount = slave.getDeviceType() == BCC_DEVICE_MC33771C ? MC33771C_MAX_CELLS : MC33772C_MAX_CELLS;

                uint16_t regValue = 0;
                for (uint8_t i = 0; i < slave.getCellCount(); i++) {
                    regValue |= (1 << i);
                }

                // Set Common OV/UV threshold bits
                regValue |= 0xC000;

                if (BCC_Communication::regWrite(slave.getCID(), MC33771C_OV_UV_EN_OFFSET, regValue) != BCC_STATUS_SUCCESS) {
                    return false;
                }

                
            }

            // Clear fault bits
            if (BCC_Communication::regWriteGlobal(MC33771C_CELL_OV_FLT_OFFSET, 0x0000U) != BCC_STATUS_SUCCESS \
                || BCC_Communication::regWriteGlobal(MC33771C_CELL_UV_FLT_OFFSET, 0x0000U) != BCC_STATUS_SUCCESS \
                || BCC_Communication::regWriteGlobal(MC33771C_AN_OT_UT_FLT_OFFSET, 0x0000U) != BCC_STATUS_SUCCESS \ 
                || BCC_Communication::regWriteGlobal(MC33771C_FAULT1_STATUS_OFFSET, 0x0000U) != BCC_STATUS_SUCCESS \
                || BCC_Communication::regWriteGlobal(MC33771C_FAULT2_STATUS_OFFSET, 0x0000U) != BCC_STATUS_SUCCESS \
                || BCC_Communication::regWriteGlobal(MC33771C_FAULT3_STATUS_OFFSET, 0x0000U) != BCC_STATUS_SUCCESS) {
                return false;
            }

            return true;
        };

        bool ADCConversions();
        bool performCellBalancing();
        bool diagnostics();
        bool allCIDsPresence();

        void runningLoop()
        {

            while (true)
            {
                // 3. ADC conversions
                ADCConversions();

                // 4. Cell balancing
                performCellBalancing();

                // 5. Diagnostics
                diagnostics();

                // 6. CID presence check
                if (!allCIDsPresence())
                {
                    return;
                }
            }
        }

        bcc_status_t globalSoftwareReset()
        {
            return BCC_Communication::regWriteGlobal(MC33771C_SYS_CFG1_OFFSET,
                                                     MC33771C_SYS_CFG1_SOFT_RST(MC33771C_SYS_CFG1_SOFT_RST_ACTIVE_ENUM_VAL));
        }

        /*!
         * @brief This function wakes device(s) up, resets them (if needed), assigns
         * CIDs and checks the communication.
         *
         * @return bcc_status_t Error code.
         */
        bcc_status_t InitDevices()
        {
            uint8_t cid;
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

            status = mSlaves[0].assignCid(loopbackMode, mSlaves.size());
            if (status != BCC_STATUS_SUCCESS)
            {
                return status;
            }

            /* Init the rest of devices. */
            for (uint8_t i = 1; i <= mSlaves.size(); i++)
            {
                BCC_MCU_WaitMs(2U);

                /* Move the following device from IDLE to NORMAL mode (in case the
                 * devices are in IDLE mode).
                 * Note that the WAKE-UP sequence is recognised as two wrong SPI
                 * transfers in devices which are already in the NORMAL mode. That will
                 * increase their COM_STATUS[COM_ERR_COUNT]. */
                BCC_Communication::wakeUpPattern(mSlaves.size());

                status = mSlaves[i].assignCid(loopbackMode, mSlaves.size());
                if (status != BCC_STATUS_SUCCESS)
                {
                    return status;
                }
            }

            return status;
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
            return BCC_Communication::regWriteGlobal(MC33771C_SYS_CFG_GLOBAL_OFFSET, MC33771C_SYS_CFG_GLOBAL_GO2SLEEP(MC33771C_SYS_CFG_GLOBAL_GO2SLEEP_ENABLED_ENUM_VAL));
        }

        /*!
         * @brief This function starts ADC conversion for all devices in TPL chain. It
         * uses a Global Write command to set ADC_CFG register. Intended for TPL mode
         * only!
         *
         * You can use function BCC_Meas_IsConverting to check conversion status and
         * function BCC_Meas_GetRawValues to retrieve the measured values.
         *
         * @param adcCfgValue Value of ADC_CFG register to be written to all devices in
         *                    the chain. Note that SOC bit is automatically added by
         *                    this function.
         *
         * @return bcc_status_t Error code.
         */
        bcc_status_t Meas_StartConversionGlobal(uint16_t adcCfgValue)
        {
            /* Set Start of Conversion bit in case it is not. */
            adcCfgValue |= MC33771C_ADC_CFG_SOC(MC33771C_ADC_CFG_SOC_ENABLED_ENUM_VAL);

            return BCC_Communication::regWriteGlobal(MC33771C_ADC_CFG_OFFSET, adcCfgValue);
        }

        /**
         * @brief freeRTOS task for SlaveController 
        */
        void task(void *argument)
        {
            while (true)
            {
                // 1. Daisy chain / CID initialization
                if (!InitDevices() != BCC_STATUS_SUCCESS)
                {
                    mCurrentFault = CID_INITIALIZATION_FAULT;
                    break;
                }
                mCurrentFault = NO_FAULT;

                // 2. Register initialization
                if (!initializeRegisters()) {
                    mCurrentFault = REGISTER_INITIALIZATION_FAULT;
                }

                // main loop
                runningLoop();
            }
        }

        /**
         * @brief Load the configuration from the UserSettings.h file
         * @return true if the configuration is valid, false otherwise
         */
        bool loadConfig()
        {
            loopbackMode = DEFAULT_SETTINGS.LOOPBACK_MODE;
            safetyLimits = DEFAULT_SETTINGS.SAFETY_LIMITS;

            // TODO implement check if shunt resistance value is within spec
            shuntResistance = DEFAULT_SETTINGS.SHUNT_RESISTANCE_OHM;

            uint8_t cid = 1U;
            for (int i = 0; i < DEFAULT_SETTINGS.SLAVE_CONFIG.size(); i++)
            {
                SlaveConfig_t slaveConfig = DEFAULT_SETTINGS.SLAVE_CONFIG[i];

                mSlaves.push_back(
                    BCC(
                        slaveConfig.DEVICE_TYPE,
                        slaveConfig.CELL_COUNT,
                        slaveConfig.NTC_COUNT,
                        slaveConfig.CURRENT_SENSING_ENABLED,
                        (bcc_cid_t)cid++));

                // Check config of slave last pushed to the array
                if (!mSlaves.back().hasValidConfig())
                {
                    return false;
                }
            }
        }
    }

    /*******************************************************************************
     * Public functions
     ******************************************************************************/
    void SlaveController::setup()
    {
        bcc_status_t status;
        if (!loadConfig())
        {
            mCurrentFault = INVALID_SLAVE_CONFIG;

            return;
        }

        status = BCC_Communication::TPL_Enable();

        if (status != BCC_STATUS_SUCCESS)
        {
            mCurrentFault = TPL_FAULT;
            return;
        }

        // Create task 'n stuff
        bmsTaskHandle = osThreadNew(task, NULL, &bmsTask_attributes);
    }
}
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

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/* CRITICAL SETTINGS */

/*! @brief Number of slaves connected to the TPL Chain. Maximum of 64 */
#define BMS_SLAVE_COUNT 2

/*! @brief Loopback mode used */
#define BMS_LOOPBACK_MODE false

/* NO NEED TO CHANGE THE SETTINGS BELOW */

/*! @brief Time after VPWR connection for the IC to be ready for initialization
 *  (t_VPWR(READY), max.) in [ms]. */
#define BCC_T_VPWR_READY_MS 5U

namespace SlaveController
{
    namespace
    {
        /*******************************************************************************
         * Private static objects
         ******************************************************************************/

        BMSFault mCurrentFault = NO_FAULT; /* Current fault state */

        BCC mSlaves[BMS_SLAVE_COUNT] = { /* Array of all BCCs */
            BCC(BCC_DEVICE_MC33771C, 12),
            BCC(BCC_DEVICE_MC33771C, 12)
        };

        /*******************************************************************************
         * Private functions
         ******************************************************************************/

        bool initializeRegisters();
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
            BCC_Communication::wakeUpPattern(BMS_SLAVE_COUNT);

            /* Software Reset all configured devices (in case they are already initialized).
             * If the devices are not initialized (CID is equal to 000000b), a write
             * command is sent via communication interface, but the software reset is
             */
            (void)globalSoftwareReset();

            /* Wait for 5 ms - for the IC to be ready for initialization. */
            BCC_MCU_WaitMs(BCC_T_VPWR_READY_MS);

            /* Assign CID to the first node and terminate its RDTX_OUT if only one
             * device is utilised and if loop-back is not required. */

            status = mSlaves[0].assignCid(BMS_LOOPBACK_MODE, BMS_SLAVE_COUNT);
            if (status != BCC_STATUS_SUCCESS)
            {
                return status;
            }

            /* Init the rest of devices. */
            for (uint8_t i = 1; i <= BMS_SLAVE_COUNT; i++)
            {
                BCC_MCU_WaitMs(2U);

                /* Move the following device from IDLE to NORMAL mode (in case the
                 * devices are in IDLE mode).
                 * Note that the WAKE-UP sequence is recognised as two wrong SPI
                 * transfers in devices which are already in the NORMAL mode. That will
                 * increase their COM_STATUS[COM_ERR_COUNT]. */
                BCC_Communication::wakeUpPattern(BMS_SLAVE_COUNT);

                status = mSlaves[i].assignCid(BMS_LOOPBACK_MODE, BMS_SLAVE_COUNT);
                if (status != BCC_STATUS_SUCCESS)
                {
                    return status;
                }
            }

            return status;
        }

        /*!
         * @brief This function initializes the battery cell controller device(s),
         * assigns CID and initializes internal driver data.
         *
         * Note that this function initializes only the INIT register of connected
         * BCC device(s).
         *
         * Note that the function is implemented for an universal use. It is capable to
         * move BCC devices into the NORMAL mode from all modes (INIT, IDLE, SLEEP and
         * NORMAL) except the RESET mode. Such implementation can generate more wake-up
         * sequences via CSB_TX pin than required for the current mode. These extra
         * sequences project to the COM_ERR_COUNT bit field in COM_STATUS register.
         * Therefore, it is recommended to clear the COM_STATUS register after calling
         * of this function.
         *
         *
         * @return bcc_status_t Error code.
         */
        bcc_status_t Init()
        {
            uint8_t dev;

            for (uint8_t i; i < BMS_SLAVE_COUNT; i++)
            {
                if (!&mSlaves[i]->hasValidConfig())
                {
                    return BCC_STATUS_PARAM_RANGE;
                }
            }

            /* Enable MC33664 device in TPL mode. */
            return BCC_Communication::TPL_Enable();
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
    }

    /*******************************************************************************
     * Public functions
     ******************************************************************************/

    void mainTask()
    {
        // First attempt mirrors recommend flow according to Programming guide
        if (Init() != BCC_STATUS_SUCCESS)
        {
            // TODO invalid config or something, exit!
        }

        while (true)
        {
            // 1. Daisy chain / CID initialization
            while (!InitDevices() != BCC_STATUS_SUCCESS)
            {
                mCurrentFault = CID_INITIALIZATION_FAULT;
            }
            mCurrentFault = NO_FAULT;

            // 2. Register initialization
            initializeRegisters();

            // main loop
            runningLoop();
        }
    }
}
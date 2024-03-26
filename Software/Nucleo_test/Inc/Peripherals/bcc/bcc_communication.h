#pragma once

#include "bcc_utils.h"

/* MACROS */

#define BCC_MCU_WriteCsbPin(value) HAL_GPIO_WritePin(BCC_SPI_TX_CS_PORT, BCC_SPI_TX_CS_PIN, value ? GPIO_PIN_SET : GPIO_PIN_RESET)
#define BCC_MCU_WriteEnPin(value) HAL_GPIO_WritePin(BCC_EN_PORT, BCC_EN_PIN, value ? GPIO_PIN_SET : GPIO_PIN_RESET)
#define BCC_MCU_ReadIntbPin() HAL_GPIO_ReadPin(BCC_INT_PORT, BCC_INT_PIN)

namespace BCC_Communication
{
     /*!
     * @brief This function intializes the SPI busses
     */
    void setup();

    /*!
     * @brief This function reads desired number of registers of the BCC device.
     *
     * In case of simultaneous read of more registers, address is incremented
     * in ascending manner.
     *
     * @param cid       Cluster Identification Address of the BCC device.
     * @param regAddr   Register address. See MC33771C.h and MC33772C.h header files
     *                  for possible values (MC3377*C_*_OFFSET macros).
     * @param regCnt    Number of registers to read.
     * @param regVal    Pointer to memory where content of selected 16 bit registers
     *                  will be stored.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t regRead(uint8_t &msgCnt, const bcc_cid_t cid, const uint8_t regAddr, const uint8_t regCnt, uint16_t *regVal);

    /*!
     * @brief This function writes a value to addressed register of the BCC device.
     *
     * @param cid       Cluster Identification Address of the BCC device.
     * @param regAddr   Register address. See MC33771C.h and MC33772C.h header files
     *                  for possible values (MC3377*C_*_OFFSET macros).
     * @param regVal    New value of selected register.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t regWrite(const bcc_cid_t cid, const uint8_t regAddr, const uint16_t regVal);

    /*!
     * @brief This function writes a value to addressed register of all configured
     * BCC devices in the chain.
     *
     * @param regAddr   Register address. See MC33771C.h and MC33772C.h header files
     *                  for possible values (MC3377*C_*_OFFSET macros).
     * @param regVal    New value of selected register.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t regWriteGlobal(const uint8_t regAddr, const uint16_t regVal);

    /*!
     * @brief This function updates content of a selected register; affects bits
     * specified by a bit mask only.
     *
     * @param cid       Cluster Identification Address of the BCC device.
     * @param regAddr   Register address. See MC33771C.h and MC33772C.h header files
     *                  for possible values (MC3377*C_*_OFFSET macros).
     * @param regMask   Bit mask. Bits set to 1 will be updated.
     * @param regVal    New value of register bits defined by bit mask.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t regUpdate(uint8_t &msgCnt, const bcc_cid_t cid, const uint8_t regAddr, const uint16_t regMask, const uint16_t regVal);

    /*!
     * @brief This function sends a No Operation command to the BCC device.
     *
     * @param cid       Cluster Identification Address of the BCC device.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t sendNop(const bcc_cid_t cid);

    /*!
     * @brief This function does two consecutive transitions of CSB_TX from low to
     * high.
     * 
     * @param devicesCnt Number of devices in the chain.
     *
     * CSB_TX -> 0 for 25 us
     * CSB_TX -> 1 for 600 us
     * CSB_TX -> 0 for 25 us
     * CSB_TX -> 1 for 750*numberOfDevices us
     *
     */
    void wakeUpPattern(size_t devicesCnt);

    /*!
     * @brief This function enables MC33664 device (sets a normal mode).
     * Intended for TPL mode only!
     *
     * During the driver initialization (BCC_Init function), normal mode of MC33664
     * is set automatically. This function can be used when sleep mode of BCC
     * device(s) and MC33664 together is required. The typical function flow is as
     * follows: BCC_Sleep -> BCC_TPL_Disable -> ... -> BCC_TPL_Enable -> BCC_WakeUp.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t TPL_Enable();

    /*!
     * @brief This function puts MC33664 device into the sleep mode.
     * Intended for TPL mode only!
     *
     * This function can be (optionally) used after BCC_Sleep function. Function
     * BCC_TPL_Enable must be then called before BCC_WakeUp function!
     *
     * @param drvInstance BCC driver instance (content of the bcc_drv_config_t
     *                    structure). Passed to the external functions defined by
     *                    the user.
     */
    void TPL_Disable();

    /*! @} */
};
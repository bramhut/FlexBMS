#pragma once

#include "bcc_utils.h"

/* IMPORTANT SETTINGS */

/*! @brief HAL SPI handlers. */
#define BCC_TX_HSPI hspi2
#define BCC_RX_HSPI hspi3

/*! @brief SPI TX CS pins. */
#define BCC_SPI_TX_CS_PORT SPI2_CS_GPIO_Port
#define BCC_SPI_TX_CS_PIN SPI2_CS_Pin

/*! @brief EN and INT pins. */
#define BCC_EN_PORT MC_EN_GPIO_Port
#define BCC_EN_PIN MC_EN_Pin
#define BCC_INT_PORT MC_INT_GPIO_Port
#define BCC_INT_PIN MC_INT_Pin

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
    bcc_status_t regRead(const bcc_cid_t cid, const uint8_t regAddr, const uint8_t regCnt, uint16_t *regVal);

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
    bcc_status_t regUpdate(const bcc_cid_t cid, const uint8_t regAddr, const uint16_t regMask, const uint16_t regVal);

    /*!
     * @brief This function sends a No Operation command to the BCC device.
     *
     * @param cid       Cluster Identification Address of the BCC device.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t sendNop(const bcc_cid_t cid);

    /*! @} */
};
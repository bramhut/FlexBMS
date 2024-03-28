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


    /**
     * @brief Calculates the CRC for the given data.
     *
     * This function takes a pointer to an array of uint8_t data and calculates the CRC value
     * using a specific algorithm.
     *
     * @param data Pointer to the data array for which the CRC needs to be calculated.
     * 
     * @return The calculated CRC value as a uint8_t.
     */
    uint8_t calcCRC(const uint8_t *const data);

    /**
     * @brief Checks the CRC of the given response.
     *
     * This function takes a pointer to an array of uint8_t response data and checks the CRC value
     * using a specific algorithm.
     *
     * @param resp Pointer to the response array for which the CRC needs to be checked.
     * 
     * @return The status of the CRC check as a bcc_status_t.
     */
    bcc_status_t checkCRC(const uint8_t *const resp);

    /**
     * @brief Checks the echo frame of the given response.
     *
     * This function takes a pointer to an array of uint8_t response data and checks the echo frame
     * using a specific algorithm.
     *
     * @param txBuf Pointer to the transmit buffer.
     * 
     * @return The status of the echo frame check as a bcc_status_t.
     */
    bcc_status_t checkEchoFrame(const uint8_t *const txBuf);

    /*!
     * @brief This function packs the frame to be sent to the BCC device.
     *
     * @param data       Data to be send.
     * @param addr       Address of the BCC device.
     * @param cid        CID.
     * @param cmdCnt     Command counter.
     * @param frame      Pointer to memory where the frame will be stored.
     *
     * @return bcc_status_t Error code.
     */
    void packFrame(const uint16_t data, const uint8_t addr, const bcc_cid_t cid, const uint8_t cmdCnt, uint8_t *const frame);

    /*!
     * @brief This function transfers data over TPL.
     *
     * @param txBuf Pointer to the transmit buffer.
     * @param rxTrCnt Number of RX messages to expect. 
     * 
     * @return bcc_status_t Error code.
     */
    bcc_status_t transfer(uint8_t txBuf[], const uint16_t rxTrCnt);

    /*!
     * @brief This function returns a pointer to the received data.
     * 
     * @return const uint8_t* Pointer to the received data.
     */
    const uint8_t* getRxBuf();

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
/*!
 * File: bcc_peripherals.h
 *
 * This file implements functions for LPSPI and GPIO operations required by BCC
 * driver. 
 */

#ifndef BCC_PERIPHERIES_H_
#define BCC_PERIPHERIES_H_


#include "bcc_wait.h"
#include "bcc.h"

/*******************************************************************************
 * API
 ******************************************************************************/
/*!
 * @addtogroup function_group
 * @{
 */

/*!
 * @brief Starts a non-blocking timeout mechanism. After expiration of the time
 * passed as a parameter, function BCC_MCU_TimeoutExpired should signalize an
 * expired timeout.
 *
 * @param timeoutUs Length of the timeout in microseconds.
 *
 * @return Returns BCC_STATUS_TIMEOUT_START in case of error, BCC_STATUS_SUCCESS
 *         otherwise.
 */
bcc_status_t BCC_MCU_StartTimeout(uint32_t timeoutUs);

/*!
 * @brief Returns state of the timeout mechanism started by the function
 * BCC_MCU_StartTimeout.
 *
 * @return True if timeout expired, false otherwise.
 */
bool BCC_MCU_TimeoutExpired(void);

/*!
  * @brief This function performs one 48b transfer via SPI bus. Intended for SPI
  * mode only. 
  *
  * The byte order of buffers is given by BCC_MSG_* macros (in bcc.h).
  *
  * @param drvInstance Instance of BCC driver.
  * @param txBuf       Pointer to TX data buffer (of BCC_MSG_SIZE size).
  * @param rxBuf       Pointer to RX data buffer (of BCC_MSG_SIZE size).
  *
  * @return bcc_status_t Error code.
  */
bcc_status_t BCC_MCU_TransferSpi(const uint8_t drvInstance, uint8_t txBuf[],
    uint8_t rxBuf[]);

/*!
 * @brief This function sends and receives data via TX and RX SPI buses.
 * Intended for TPL mode only.
 *
 * TX SPI bus always performs only one 48b SPI transfer. Expected number of RX
 * transfers is passed as a parameter. The byte order of buffers is given by
 * BCC_MSG_* macros (in bcc.h).
 *
 * @param drvInstance Instance of BCC driver.
 * @param txBuf       Pointer to TX data buffer (of BCC_MSG_SIZE size).
 * @param rxBuf       Pointer to buffer for received data. Its size is at least
 *                    (BCC_MSG_SIZE * recvTrCnt) bytes.
 * @param rxTrCnt     Number of 48b transfers to be received. A non-zero value.
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC_MCU_TransferTpl(const uint8_t drvInstance, uint8_t txBuf[],
    uint8_t rxBuf[], const uint16_t rxTrCnt);

/*!
 * @brief User implementation of assert.
 *
 * @param x - True if everything is OK.
 */
void BCC_MCU_Assert(const bool x);

/*!
 * @brief Writes logic 0 or 1 to the CSB (SPI mode) or CSB_TX pin (TPL mode).
 *
 * @param drvInstance Instance of BCC driver.
 * @param value       Zero or one to be set to CSB (CSB_TX) pin.
 */
void BCC_MCU_WriteCsbPin(const uint8_t drvInstance, const uint8_t value);

/*!
 * @brief Writes logic 0 or 1 to the RST pin.
 *
 * @param drvInstance Instance of BCC driver.
 * @param value       Zero or one to be set to RST pin.
 */
void BCC_MCU_WriteRstPin(const uint8_t drvInstance, const uint8_t value);

/*!
 * @brief Writes logic 0 or 1 to the EN pin of MC33664.
 *
 * @param drvInstance Instance of BCC driver.
 * @param value       Zero or one to be set to EN pin.
 */
void BCC_MCU_WriteEnPin(const uint8_t drvInstance, const uint8_t value);

/*!
 * @brief Reads logic value of INTB pin of MC33664.
 *
 * @param drvInstance Instance of BCC driver.
 *
 * @return Zero value for logic zero, non-zero value otherwise.
 */
uint32_t BCC_MCU_ReadIntbPin(const uint8_t drvInstance);
/*! @} */

#endif /* BCC_PERIPHERIES_H_ */
/*******************************************************************************
 * EOF
 ******************************************************************************/

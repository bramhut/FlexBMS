
/*
 * File: bcc_peripherals.cpp
 *
 * This file implements functions for LPSPI and GPIO operations required by BCC
 * driver.
 */

/*******************************************************************************
 * Includes
 ******************************************************************************/


#include "bcc/bcc_peripherals.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/* Timeout for sending one 48b frame via SPI TX in milliseconds. */
#define BCC_TX_COM_TIMEOUT_MS     1

/* Timeout for SPI TX communication in milliseconds. Note that the maximal
 * transfer (receiving of 127 registers) takes 4 ms. Another 0.95 us
 * (t_port_delay) is introduced by each repeater in 33771. */
#define BCC_RX_COM_TIMEOUT_MS     10

/* EN - PTB0 (TPL1 interface) */
#define BCC_EN1_INSTANCE          PTB
#define BCC_EN1_INDEX             0

/* INTB - PTB1 (TPL1 interface) */
#define BCC_INTB1_INSTANCE        PTB
#define BCC_INTB1_INDEX           1

/* CSB_TX1 - PTA6/LPSPI1_PCS1 (TPL1 interface) */
#define BCC_TX1_LPSPI_PCS         LPSPI_PCS1




/*******************************************************************************
 * Global variables
 ******************************************************************************/

static bool s_timeoutExpired;

/*******************************************************************************
 * IRQ handlers
 ******************************************************************************/

/*!
 * @brief LPIT interrupt handler.
 * When an interrupt occurs stop timer, clear channel flag and set a flag.
 */
void LPIT0_Ch3_IRQHandler(void)
{
    /* Stop LPIT0 channel counter. */
    LPIT_DRV_StopTimerChannels(INST_LPIT1, (1 << LPIT_CHANNEL));

    /* Clear LPIT channel flag. */
    LPIT_DRV_ClearInterruptFlagTimerChannels(INST_LPIT1, (1 << LPIT_CHANNEL));

    s_timeoutExpired = true;
}

/*******************************************************************************
 * Code
 ******************************************************************************/

/*FUNCTION**********************************************************************
 *
 * Function Name : BCC_MCU_StartTimeout
 * Description   : Starts a non-blocking timeout mechanism. After expiration of
 *                 the time passed as a parameter, function
 *                 BCC_MCU_TimeoutExpired should signalize an expired timeout.
 *
 *END**************************************************************************/
bcc_status_t BCC_MCU_StartTimeout(const uint32_t timeoutUs)
{
    status_t status;

    /* Stop LPIT0 channel 3 counter. */
    LPIT_DRV_StopTimerChannels(INST_LPIT1, (1 << LPIT_CHANNEL));

    /* Initialize LPIT channel 3 and configure it as a periodic counter
     * which is used to generate an interrupt. */
    status = LPIT_DRV_SetTimerPeriodByUs(INST_LPIT1, LPIT_CHANNEL, timeoutUs);
    if (status != STATUS_SUCCESS)
    {
        return BCC_STATUS_TIMEOUT_START;
    }

    s_timeoutExpired = false;

    /* Start LPIT0 channel 3 counter */
    LPIT_DRV_StartTimerChannels(INST_LPIT1, (1 << LPIT_CHANNEL));

    return BCC_STATUS_SUCCESS;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : BCC_MCU_TimeoutExpired
 * Description   : This function returns state of the timeout mechanism started
 *                 by the function BCC_MCU_StartTimeout.
 *
 *END**************************************************************************/
bool BCC_MCU_TimeoutExpired(void)
{
    return s_timeoutExpired;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : BCC_MCU_TransferSpi
 * Description   : This function sends and receives data via SPI bus. Intended
 *                 for SPI mode only.
 *
 *END**************************************************************************/
bcc_status_t BCC_MCU_TransferSpi(const uint8_t drvInstance, uint8_t txBuf[],
    uint8_t rxBuf[])
{
#if defined(SPI)
    status_t error;

    DEV_ASSERT(txBuf != NULL);
    DEV_ASSERT(rxBuf != NULL);

    error = LPSPI_DRV_MasterTransferBlocking(LPSPISPI_TPL1RX, txBuf, rxBuf,
            LPSPI_ALIGNMENT, BCC_COM_TIMEOUT_MS);
    if (error != STATUS_SUCCESS)
    {
        return (error == STATUS_TIMEOUT) ? BCC_STATUS_COM_TIMEOUT : BCC_STATUS_SPI_FAIL;
    }

    return BCC_STATUS_SUCCESS;
#elif defined(TPL)
    return BCC_STATUS_SPI_FAIL;
#endif
}

/*FUNCTION**********************************************************************
 *
 * Function Name : BCC_MCU_TransferTpl
 * Description   : This function sends and receives data via TX and RX SPI buses.
 *                 Intended for TPL mode only.
 *
 *END**************************************************************************/
bcc_status_t BCC_MCU_TransferTpl(const uint8_t drvInstance, uint8_t txBuf[],
    uint8_t rxBuf[], const uint16_t rxTrCnt)
{
#if defined(TPL)
    int32_t rxTimeout;
    status_t error;

    DEV_ASSERT(txBuf != NULL);
    DEV_ASSERT(rxBuf != NULL);
    DEV_ASSERT(rxTrCnt > 0);

    /* Transmissions at RX and TX SPI occur almost at the same time. Start
     * reading (response) at RX SPI first. */
    error = LPSPI_DRV_SlaveTransfer(LPSPISPI_TPL1RX, NULL, rxBuf, rxTrCnt * LPSPI_ALIGNMENT);
    if (error != STATUS_SUCCESS)
    {
        return BCC_STATUS_SPI_FAIL;
    }

    /* Send data via TX SPI. */
    error = LPSPI_DRV_MasterTransferBlocking(LPSPITPLTX, txBuf, NULL,
            LPSPI_ALIGNMENT, BCC_TX_COM_TIMEOUT_MS);
    if (error != STATUS_SUCCESS)
    {
        /* Cancel reception of data. */
        LPSPI_DRV_SlaveAbortTransfer(LPSPISPI_TPL1RX);

        return (error == STATUS_TIMEOUT) ? BCC_STATUS_COM_TIMEOUT : BCC_STATUS_SPI_FAIL;
    }

    /* Wait until RX transmission finished. */
    rxTimeout = BCC_RX_COM_TIMEOUT_MS * 1000;
    while ((LPSPI_DRV_SlaveGetTransferStatus(LPSPISPI_TPL1RX, NULL)
            == STATUS_BUSY) && (rxTimeout > 0))
    {
        BCC_MCU_WaitUs(10);
        rxTimeout -= 10;
    }

    /* Cancel data reception if the timeout expires. */
    if (rxTimeout <= 0)
    {
        LPSPI_DRV_SlaveAbortTransfer(LPSPISPI_TPL1RX);
        return BCC_STATUS_COM_TIMEOUT;
    }

    return BCC_STATUS_SUCCESS;

#elif defined(SPI)
    return BCC_STATUS_SPI_FAIL;
#endif
}

/*FUNCTION**********************************************************************
 *
 * Function Name : BCC_MCU_Assert
 * Description   : User implementation of assert.
 *
 *END**************************************************************************/
void BCC_MCU_Assert(const bool x)
{
    DEV_ASSERT(x);
}

/*FUNCTION**********************************************************************
 *
 * Function Name : BCC_MCU_WriteCsbPin
 * Description   : Writes logic 0 or 1 to the CSB_TX pin.
 *
 *END**************************************************************************/
void BCC_MCU_WriteCsbPin(const uint8_t drvInstance, const uint8_t value)
{
#if defined(TPL)
    LPSPI_DRV_SetPcs(LPSPITPLTX, BCC_TX1_LPSPI_PCS,
            value ? LPSPI_ACTIVE_LOW : LPSPI_ACTIVE_HIGH);
#elif defined(SPI)
    LPSPI_DRV_SetPcs(LPSPISPI_TPL1RX, BCC_LPSPI_PCS,
            value ? LPSPI_ACTIVE_LOW : LPSPI_ACTIVE_HIGH);
#endif
}

/*FUNCTION**********************************************************************
 *
 * Function Name : BCC_MCU_WriteRstPin
 * Description   : Writes logic 0 or 1 to the RST pin.
 *
 *END**************************************************************************/
void BCC_MCU_WriteRstPin(const uint8_t drvInstance, const uint8_t value)
{
#if defined(SPI)
    PINS_DRV_WritePin(BCC_RST_INSTANCE, BCC_RST_INDEX, value);
#endif
}

/*FUNCTION**********************************************************************
 *
 * Function Name : BCC_MCU_WriteEnPin
 * Description   : Writes logic 0 or 1 to the EN pin of MC33664.
 *
 *END**************************************************************************/
void BCC_MCU_WriteEnPin(const uint8_t drvInstance, const uint8_t value)
{
#if defined(TPL)
    PINS_DRV_WritePin(BCC_EN1_INSTANCE, BCC_EN1_INDEX, value);
#endif
}

/*FUNCTION**********************************************************************
 *
 * Function Name : BCC_MCU_ReadIntbPin
 * Description   : Reads logic value of INTB pin of MC33664.
 *
 *END**************************************************************************/
uint32_t BCC_MCU_ReadIntbPin(const uint8_t drvInstance)
{
#if defined(TPL)
    return (PINS_DRV_ReadPins(BCC_INTB1_INSTANCE) >> BCC_INTB1_INDEX) & 1;
#else
    return 0U;
#endif
}

/*******************************************************************************
 * EOF
 ******************************************************************************/

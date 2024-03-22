#pragma once
#include "bcc_utils.h"
#include "SPIwrapper.h"

class BCC_Peripherals
{
private:
    SPI* spiTX;
    SPI* spiRX;

public:
    BCC_Peripherals(SPI* spiTx, SPI* spiRx) {
        this->spiTX = spiTx;
        this->spiRX = spiRx;
    }

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
     * @brief Writes logic 0 or 1 to the CSB (SPI mode) or CSB_TX pin (TPL mode).
     *
     * @param value       Zero or one to be set to CSB (CSB_TX) pin.
     */
    void BCC_MCU_WriteCsbPin(const uint8_t value);

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
};
/*!
 * @file bcc_communication.c
 *
 * This file implements the basic low-level functions for both SPI and TPL
 * communication with MC33771C and MC33772C BCC devices.
 */

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "bcc/bcc_communication.h"
#include "cmsis_os.h" // Needed for assert
#include "SPIwrapper.h"


/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*! @brief Timeout for TX communication in milliseconds. */
#define BCC_TX_COM_TIMEOUT_MS 1U

/*! @brief Timeout for RX communication in milliseconds. */
#define BCC_RX_COM_TIMEOUT_MS 1U

/*! @brief Size of CRC table. */
#define BCC_CRC_TBL_SIZE 256U

/** BCC Commands. */
/*! @brief No operation command. */
#define BCC_CMD_NOOP 0x00U
/*! @brief Read command. */
#define BCC_CMD_READ 0x01U
/*! @brief Write command. */
#define BCC_CMD_WRITE 0x02U
/*! @brief Global write command. */
#define BCC_CMD_GLOB_WRITE 0x03U

/*! @brief CSB_TX LOW period in CSB_TX wake-up pulse sequence in [us]. */
#define BCC_WAKE_PULSE_US 25U

/*! @brief Time between wake pulses (t_WAKE_DELAY, typ.) in [us]. */
#define BCC_T_WAKE_DELAY_US 600U

/*! @brief Time the MCU shall wait after sending first wake-up message
 * per 33771C/33772C IC (t_WU_Wait, min.) in [us]. */
#define BCC_T_WU_WAIT_US 750U

/*! @brief EN LOW to HIGH transition to INTB verification pulse
 * (t_INTB_PULSE_DELAY, max.) in [us]. */
#define BCC_T_INTB_PULSE_DELAY_US 100U

/*! @brief INTB verification pulse duration (t_INTB_PULSE, typ.) in [us]. */
#define BCC_T_INTB_PULSE_US 100U

/*!
 * @brief Returns data field of the communication frame.
 *
 * @param msg Pointer to the frame.
 * @return Data field.
 */
#define BCC_GET_MSG_DATA(msg)                            \
    (((uint16_t) * ((msg) + BCC_MSG_IDX_DATA_H) << 8U) | \
     (uint16_t) * ((msg) + BCC_MSG_IDX_DATA_L))

/*! @brief Mask for address field of frame. */
#define BCC_MSG_ADDR_MASK 0x7FU
/*! @brief Mask of the message counter bit field within the BCC_MSG_IDX_CNT_CMD
 *  byte. */
#define BCC_MSG_MSG_CNT_MASK 0xF0U
/*! @brief Shift of the message counter bit field within the BCC_MSG_IDX_CNT_CMD
 *  byte. */
#define BCC_MSG_MSG_CNT_SHIFT 4U

/*!
 * @brief Increments message counter value and executes modulo 16.
 *
 * @param msgCntr Message counter to be incremented.
 * @return Incremented value.
 */
#define BCC_INC_MSG_CNTR(msgCntr) \
    (((msgCntr) + 1U) & 0x0FU)

/*!
 * @brief Returns true if all bit fields except Message counter and CRC field
 * are zero.
 *
 * @param resp Response message to be checked.
 * @return True when the response is zero (except CRC and MSG_CNTR), false
 *         otherwise.
 */
#define BCC_IS_NULL_RESP(resp)             \
    (((resp)[BCC_MSG_IDX_DATA_H] == 0U) && \
     ((resp)[BCC_MSG_IDX_DATA_L] == 0U) && \
     ((resp)[BCC_MSG_IDX_ADDR] == 0U) &&   \
     ((resp)[BCC_MSG_IDX_CID] == 0U) &&    \
     (((resp)[BCC_MSG_IDX_CNT_CMD] & (~BCC_MSG_MSG_CNT_MASK)) == 0U))

/*! @brief Address of the last register. */
#define BCC_MAX_REG_ADDR 0x7FU

/*******************************************************************************
 * Constant variables
 ******************************************************************************/

/* Table with precalculated CRC values. */
static const uint8_t s_crcTable[BCC_CRC_TBL_SIZE] = {
    0x00U, 0x2fU, 0x5eU, 0x71U, 0xbcU, 0x93U, 0xe2U, 0xcdU,
    0x57U, 0x78U, 0x09U, 0x26U, 0xebU, 0xc4U, 0xb5U, 0x9aU,
    0xaeU, 0x81U, 0xf0U, 0xdfU, 0x12U, 0x3dU, 0x4cU, 0x63U,
    0xf9U, 0xd6U, 0xa7U, 0x88U, 0x45U, 0x6aU, 0x1bU, 0x34U,
    0x73U, 0x5cU, 0x2dU, 0x02U, 0xcfU, 0xe0U, 0x91U, 0xbeU,
    0x24U, 0x0bU, 0x7aU, 0x55U, 0x98U, 0xb7U, 0xc6U, 0xe9U,
    0xddU, 0xf2U, 0x83U, 0xacU, 0x61U, 0x4eU, 0x3fU, 0x10U,
    0x8aU, 0xa5U, 0xd4U, 0xfbU, 0x36U, 0x19U, 0x68U, 0x47U,
    0xe6U, 0xc9U, 0xb8U, 0x97U, 0x5aU, 0x75U, 0x04U, 0x2bU,
    0xb1U, 0x9eU, 0xefU, 0xc0U, 0x0dU, 0x22U, 0x53U, 0x7cU,
    0x48U, 0x67U, 0x16U, 0x39U, 0xf4U, 0xdbU, 0xaaU, 0x85U,
    0x1fU, 0x30U, 0x41U, 0x6eU, 0xa3U, 0x8cU, 0xfdU, 0xd2U,
    0x95U, 0xbaU, 0xcbU, 0xe4U, 0x29U, 0x06U, 0x77U, 0x58U,
    0xc2U, 0xedU, 0x9cU, 0xb3U, 0x7eU, 0x51U, 0x20U, 0x0fU,
    0x3bU, 0x14U, 0x65U, 0x4aU, 0x87U, 0xa8U, 0xd9U, 0xf6U,
    0x6cU, 0x43U, 0x32U, 0x1dU, 0xd0U, 0xffU, 0x8eU, 0xa1U,
    0xe3U, 0xccU, 0xbdU, 0x92U, 0x5fU, 0x70U, 0x01U, 0x2eU,
    0xb4U, 0x9bU, 0xeaU, 0xc5U, 0x08U, 0x27U, 0x56U, 0x79U,
    0x4dU, 0x62U, 0x13U, 0x3cU, 0xf1U, 0xdeU, 0xafU, 0x80U,
    0x1aU, 0x35U, 0x44U, 0x6bU, 0xa6U, 0x89U, 0xf8U, 0xd7U,
    0x90U, 0xbfU, 0xceU, 0xe1U, 0x2cU, 0x03U, 0x72U, 0x5dU,
    0xc7U, 0xe8U, 0x99U, 0xb6U, 0x7bU, 0x54U, 0x25U, 0x0aU,
    0x3eU, 0x11U, 0x60U, 0x4fU, 0x82U, 0xadU, 0xdcU, 0xf3U,
    0x69U, 0x46U, 0x37U, 0x18U, 0xd5U, 0xfaU, 0x8bU, 0xa4U,
    0x05U, 0x2aU, 0x5bU, 0x74U, 0xb9U, 0x96U, 0xe7U, 0xc8U,
    0x52U, 0x7dU, 0x0cU, 0x23U, 0xeeU, 0xc1U, 0xb0U, 0x9fU,
    0xabU, 0x84U, 0xf5U, 0xdaU, 0x17U, 0x38U, 0x49U, 0x66U,
    0xfcU, 0xd3U, 0xa2U, 0x8dU, 0x40U, 0x6fU, 0x1eU, 0x31U,
    0x76U, 0x59U, 0x28U, 0x07U, 0xcaU, 0xe5U, 0x94U, 0xbbU,
    0x21U, 0x0eU, 0x7fU, 0x50U, 0x9dU, 0xb2U, 0xc3U, 0xecU,
    0xd8U, 0xf7U, 0x86U, 0xa9U, 0x64U, 0x4bU, 0x3aU, 0x15U,
    0x8fU, 0xa0U, 0xd1U, 0xfeU, 0x33U, 0x1cU, 0x6dU, 0x42U};

namespace BCC_Communication
{
    // internal anonymous namespace for private functions
    namespace
    {
        /*******************************************************************************
         * Private static objects
         ******************************************************************************/
        SPI spiTX(&BCC_TX_HSPI, SPI::MASTER_TX, BCC_SPI_TX_CS_PORT, BCC_SPI_TX_CS_PIN);
        SPI spiRX(&BCC_RX_HSPI, SPI::SLAVE_RX);

        uint8_t rxBuf[BCC_RX_BUF_SIZE_TPL];      /* Buffer for received data. */

        /*******************************************************************************
         * Private functions
         ******************************************************************************/

        /*FUNCTION**********************************************************************
         *
         * Function Name : CalcCRC
         * Description   : This function calculates CRC value of passed data array.
         *
         *END**************************************************************************/
        uint8_t CalcCRC(const uint8_t *const data)
        {
            uint8_t crc;      /* Result. */
            uint8_t tableIdx; /* Index to the CRC table. */

            configASSERT(data != NULL);

            /* Expanding value. */
            crc = 0x42U;

            tableIdx = crc ^ data[BCC_MSG_IDX_DATA_H];
            crc = s_crcTable[tableIdx];
            tableIdx = crc ^ data[BCC_MSG_IDX_DATA_L];
            crc = s_crcTable[tableIdx];
            tableIdx = crc ^ data[BCC_MSG_IDX_ADDR];
            crc = s_crcTable[tableIdx];
            tableIdx = crc ^ data[BCC_MSG_IDX_CID];
            crc = s_crcTable[tableIdx];
            tableIdx = crc ^ data[BCC_MSG_IDX_CNT_CMD];
            crc = s_crcTable[tableIdx];

            return crc;
        }

        /*FUNCTION**********************************************************************
         *
         * Function Name : CheckCRC
         * Description   : This function calculates CRC of a received frame and compares
         *                 it with CRC field of the frame.
         *
         *END**************************************************************************/
        bcc_status_t CheckCRC(const uint8_t *const resp)
        {
            uint8_t frameCrc; /* CRC value from the response frame. */
            uint8_t compCrc;  /* Computed (expected) CRC value. */

            configASSERT(resp != NULL);

            frameCrc = resp[BCC_MSG_IDX_CRC];
            compCrc = CalcCRC(resp);

            return (compCrc != frameCrc) ? BCC_STATUS_COM_CRC : BCC_STATUS_SUCCESS;
        }

        /*FUNCTION**********************************************************************
         *
         * Function Name : CheckMsgCnt
         * Description   : This function checks value of the Message counter field of
         *                 a frame.
         *
         *END**************************************************************************/
        bcc_status_t CheckMsgCnt(uint8_t &msgCnt, const bcc_cid_t cid, const uint8_t *const resp)
        {
            uint8_t msgCntPrev; /* Previously received message counter value. */
            uint8_t msgCntRcv;  /* Currently received message counter value. */

            configASSERT(resp != NULL);

            msgCntPrev = msgCnt;
            msgCntRcv = (resp[BCC_MSG_IDX_CNT_CMD] & BCC_MSG_MSG_CNT_MASK) >> BCC_MSG_MSG_CNT_SHIFT;

            /* Store the Message counter value. */
            msgCnt = msgCntRcv;

            /* Check the Message counter value.
             * Note: Do not perform a check for CID=0. */
            if ((cid != BCC_CID_UNASSIG) && (msgCntRcv != BCC_INC_MSG_CNTR(msgCntPrev)))
            {
                return BCC_STATUS_COM_MSG_CNT;
            }

            return BCC_STATUS_SUCCESS;
        }

        /*FUNCTION**********************************************************************
         *
         * Function Name : CheckEchoFrame
         * Description   : This function checks content of the echo frame.
         *
         *END**************************************************************************/
        bcc_status_t CheckEchoFrame(const uint8_t *const txBuf,
                                    const uint8_t *const resp)
        {
            configASSERT(resp != NULL);
            configASSERT(txBuf != NULL);

            if ((txBuf[BCC_MSG_IDX_DATA_H] == resp[BCC_MSG_IDX_DATA_H]) &&
                (txBuf[BCC_MSG_IDX_DATA_L] == resp[BCC_MSG_IDX_DATA_L]) &&
                (txBuf[BCC_MSG_IDX_ADDR] == resp[BCC_MSG_IDX_ADDR]) &&
                (txBuf[BCC_MSG_IDX_CID] == resp[BCC_MSG_IDX_CID]) &&
                (txBuf[BCC_MSG_IDX_CNT_CMD] == resp[BCC_MSG_IDX_CNT_CMD]) &&
                (txBuf[BCC_MSG_IDX_CRC] == resp[BCC_MSG_IDX_CRC]))
            {
                return BCC_STATUS_SUCCESS;
            }
            else
            {
                return BCC_STATUS_COM_ECHO;
            }
        }

        /*FUNCTION**********************************************************************
         *
         * Function Name : PackFrame
         * Description   : This function packs all the parameters into a frame according
         *                 to the BCC frame format (see BCC datasheet).
         *
         *END**************************************************************************/
        void PackFrame(const uint16_t data, const uint8_t addr,
                       const bcc_cid_t cid, const uint8_t cmdCnt, uint8_t *const frame)
        {
            configASSERT(frame != NULL);

            /* Register Data field. */
            frame[BCC_MSG_IDX_DATA_H] = (uint8_t)(data >> 8U);
            frame[BCC_MSG_IDX_DATA_L] = (uint8_t)(data & 0xFFU);

            /* Register Address field. Master/Slave field is always 0 for sending. */
            frame[BCC_MSG_IDX_ADDR] = (addr & BCC_MSG_ADDR_MASK);

            /* Device address (Cluster ID) field. */
            frame[BCC_MSG_IDX_CID] = ((uint8_t)cid & 0x3FU);

            /* Message counter and Command fields. */
            frame[BCC_MSG_IDX_CNT_CMD] = (cmdCnt & 0xF3U);

            /* CRC field. */
            frame[BCC_MSG_IDX_CRC] = CalcCRC(frame);
        }

        /*FUNCTION**********************************************************************
         *
         * Function Name : MCU_TransferTpl
         * Description   : This function sends and receives data via TX and RX SPI buses.
         *                 Intended for TPL mode only.
         *
         *END**************************************************************************/
        bcc_status_t MCU_TransferTpl(uint8_t txBuf[],
                                     uint8_t rxBuf[], const uint16_t rxTrCnt)

        {
            BCC_MCU_Assert(txBuf != NULL);
            BCC_MCU_Assert(rxBuf != NULL);
            BCC_MCU_Assert(rxTrCnt > 0);

            // Start RX SPI
            spiRX.receive(rxBuf, rxTrCnt * 6);

            // Send data to transceiver
            if (!spiTX.transmit(txBuf, 6, BCC_TX_COM_TIMEOUT_MS))
            {
                return BCC_STATUS_COM_TIMEOUT;
            }

            // Wait for data to be available in the RX buffer
            int32_t rxTimeout = BCC_RX_COM_TIMEOUT_MS * 1000;
            while ((!spiRX.getRxDataAvailable()) && (rxTimeout > 0))
            {
                BCC_MCU_WaitUs(10);
                rxTimeout -= 10;
            }

            // Succes! Data is now available in the RX buffer
            return BCC_STATUS_SUCCESS;
        }
    }

    /*FUNCTION**********************************************************************
     *
     * Function Name : setup
     * Description   : Setup the SPI communication.
     *
     *END**************************************************************************/
    void setup()
    {
        spiTX.setup();
        spiRX.setup();
    }

    /*FUNCTION**********************************************************************
     *
     * Function Name : reg_ReadTpl
     * Description   : This function reads desired number of registers of the BCC
     *                 device. Intended for TPL mode only.
     *
     *END**************************************************************************/
    bcc_status_t regRead(uint8_t &msgCnt, const bcc_cid_t cid, const uint8_t regAddr, const uint8_t regCnt, uint16_t *regVal)
    {
        uint8_t txBuf[BCC_MSG_SIZE]; /* TX buffer. */
        uint8_t regIdx;              /* Index of a received register. */
        bcc_status_t status;

        configASSERT(regVal != NULL);

        if ((regAddr > BCC_MAX_REG_ADDR) ||
            (regCnt == 0U) || ((regAddr + regCnt - 1U) > BCC_MAX_REG_ADDR))
        {
            return BCC_STATUS_PARAM_RANGE;
        }

        /* Create frame for request. */
        PackFrame((uint16_t)regCnt, regAddr, cid, BCC_CMD_READ, txBuf);

        status = MCU_TransferTpl(txBuf, rxBuf, regCnt + 1);
        if (status != BCC_STATUS_SUCCESS)
        {
            return status;
        }

        /* Check the echo frame. */
        status = CheckEchoFrame(txBuf, rxBuf);
        if (status != BCC_STATUS_SUCCESS)
        {
            return status;
        }

        /* Check and store responses. */
        uint8_t *rxBufMsgIdx = rxBuf;
        for (regIdx = 0U; regIdx < regCnt; regIdx++)
        {
            rxBufMsgIdx += BCC_MSG_SIZE;

            /* Check CRC. */
            if ((status = CheckCRC(rxBufMsgIdx)) != BCC_STATUS_SUCCESS)
            {
                return status;
            }

            /* Check the Message counter value. */
            if ((status = CheckMsgCnt(msgCnt, cid, rxBufMsgIdx)) != BCC_STATUS_SUCCESS)
            {
                return status;
            }

            /* Store data. */
            *regVal++ = BCC_GET_MSG_DATA(rxBuf);
        }

        return BCC_STATUS_SUCCESS;
    }

    /*FUNCTION**********************************************************************
     *
     * Function Name : reg_Write
     * Description   : This function writes a value to addressed register of the
     *                 BCC device. Intended for TPL mode only.
     *
     *END**************************************************************************/
    bcc_status_t regWrite(const bcc_cid_t cid, const uint8_t regAddr, const uint16_t regVal)
    {
        uint8_t txBuf[BCC_MSG_SIZE]; /* Transmission buffer. */
        bcc_status_t status;

        if (regAddr > BCC_MAX_REG_ADDR)
        {
            return BCC_STATUS_PARAM_RANGE;
        }

        /* Create frame for writing. */
        PackFrame(regVal, regAddr, cid, BCC_CMD_WRITE, txBuf);

        status = MCU_TransferTpl(txBuf, rxBuf, 1);
        if (status != BCC_STATUS_SUCCESS)
        {
            return status;
        }

        /* Check the echo frame. */
        return CheckEchoFrame(txBuf, rxBuf);
    }

    /*FUNCTION**********************************************************************
     *
     * Function Name : reg_WriteGlobalTpl
     * Description   : This function writes a value to addressed register of all
     *                 configured BCC devices in the chain. Intended for TPL mode
     *                 only.
     *
     *END**************************************************************************/
    bcc_status_t regWriteGlobal(const uint8_t regAddr, const uint16_t regVal)
    {
        uint8_t txBuf[BCC_MSG_SIZE]; /* Transmission buffer. */
        bcc_status_t status;

        /* Check input parameters. */
        if (regAddr > BCC_MAX_REG_ADDR)
        {
            return BCC_STATUS_PARAM_RANGE;
        }

        /* Create frame for writing. */
        PackFrame(regVal, regAddr, BCC_CID_UNASSIG, BCC_CMD_GLOB_WRITE, txBuf);

        status = MCU_TransferTpl(txBuf, rxBuf, 1);
        if (status != BCC_STATUS_SUCCESS)
        {
            return status;
        }

        /* Check the echo frame. */
        return CheckEchoFrame(txBuf, rxBuf);
    }

    /*FUNCTION**********************************************************************
     *
     * Function Name : reg_Update
     * Description   : This function updates content of a selected register; affects
     *                 bits specified by a bit mask only.
     *
     *END**************************************************************************/
    bcc_status_t regUpdate(uint8_t &msgCnt, const bcc_cid_t cid, const uint8_t regAddr, const uint16_t regMask, const uint16_t regVal)
    {
        uint16_t regValTemp;
        bcc_status_t status;

        status = regRead(msgCnt, cid, regAddr, 1U, &regValTemp);
        if (status != BCC_STATUS_SUCCESS)
        {
            return status;
        }

        /* Update register value. */
        regValTemp = regValTemp & ~(regMask);
        regValTemp = regValTemp | (regVal & regMask);

        return regWrite(cid, regAddr, regValTemp);
    }

    /*FUNCTION**********************************************************************
     *
     * Function Name : sendNop
     * Description   : This function sends a No Operation command (NOP) to the
     *                 BCC device. Intended for TPL mode only.
     *
     *END**************************************************************************/
    bcc_status_t sendNop(const bcc_cid_t cid)
    {
        uint8_t txBuf[BCC_MSG_SIZE]; /* Transmission buffer. */
        bcc_status_t status;

        /* Create frame for writing.
         * Note: Register Data, Register Address and Message counter fields can
         * contain any value. */
        PackFrame(0x0000U, 0x00U, cid, BCC_CMD_NOOP, txBuf);

        status = MCU_TransferTpl(txBuf, rxBuf, 1);
        if (status != BCC_STATUS_SUCCESS)
        {
            return status;
        }

        /* Check the echo frame. */
        return CheckEchoFrame(txBuf, rxBuf);
    }

    /*FUNCTION**********************************************************************
     *
     * Function Name : wakeUpPattern
     * Description   : This function does two consecutive transitions of CSB_TX from
     *                 low to high.
     *
     *END**************************************************************************/
    void wakeUpPattern(size_t devicesCnt)
    {
        /* CSB_TX low for 25 us. */
        BCC_MCU_WriteCsbPin(0);
        BCC_MCU_WaitUs(BCC_WAKE_PULSE_US);

        /* CSB_TX high for 600 us. */
        BCC_MCU_WriteCsbPin(1);
        BCC_MCU_WaitUs(BCC_T_WAKE_DELAY_US);

        /* CSB_TX low for 25 us. */
        BCC_MCU_WriteCsbPin(0);
        BCC_MCU_WaitUs(BCC_WAKE_PULSE_US);

        /* CSB_TX high. */
        BCC_MCU_WriteCsbPin(1);
        /* Time to switch Sleep mode to normal mode after TPL bus wake-up. */
        BCC_MCU_WaitUs(BCC_T_WU_WAIT_US * devicesCnt);
    }

    /*FUNCTION**********************************************************************
     *
     * Function Name : TPL_Enable
     * Description   : This function enables MC33664 TPL device. It uses EN and
     *                 INTB pins. Intended for TPL mode only!
     *
     *END**************************************************************************/
    bcc_status_t TPL_Enable()
    {
        int32_t timeout;

        /* Set normal state (transition from low to high). */
        BCC_MCU_WriteEnPin(0);
        /* Wait at least 100 us. */
        BCC_MCU_WaitUs(150);
        BCC_MCU_WriteEnPin(1);

        /* Note: MC33664 has time t_Ready/t_INTB_PULSE_DELAY (max. 100 us) to take effect.
         * Wait for INTB transition from high to low (max. 100 us). */
        timeout = BCC_T_INTB_PULSE_DELAY_US;
        while ((BCC_MCU_ReadIntbPin() > 0) && (timeout > 0))
        {
            timeout -= 5;
            BCC_MCU_WaitUs(5U);
        }
        if ((timeout <= 0) && (BCC_MCU_ReadIntbPin() > 0))
        {
            return BCC_STATUS_COM_TIMEOUT;
        }

        /* Wait for INTB transition from low to high (typ. 100 us).
         * Wait for at most 200 us. */
        timeout = BCC_T_INTB_PULSE_US * 2;
        while ((BCC_MCU_ReadIntbPin() == 0) && (timeout > 0))
        {
            timeout -= 10;
            BCC_MCU_WaitUs(10U);
        }
        if ((timeout <= 0) && (BCC_MCU_ReadIntbPin() == 0))
        {
            return BCC_STATUS_COM_TIMEOUT;
        }

        /* Now the device should be in normal mode (i.e. after INTB low to high
         * transition). For sure wait for 150 us. */
        BCC_MCU_WaitUs(150U);

        return BCC_STATUS_SUCCESS;
    }

    /*FUNCTION**********************************************************************
     *
     * Function Name : TPL_Disable
     * Description   : This function puts MC33664 device into the sleep mode.
     *                 Intended for TPL mode only!
     *
     *END**************************************************************************/
    void TPL_Disable()
    {
        BCC_MCU_WriteEnPin(0);
    }

}

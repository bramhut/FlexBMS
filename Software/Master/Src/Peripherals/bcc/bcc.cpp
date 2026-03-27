/*!
 * @file bcc.c
 *
 * Battery cell controller SW driver for MC33771C and MC33772C v2.2.
 */

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "bcc/bcc_communication.h"
#include "bcc/bcc.h"
#include "bcc/UserSettings.h"

#define DEBUG_LVL 2
#include "Debug.h"

using std::vector;

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/** BCC Commands. */
/*! @brief No operation command. */
#define BCC_CMD_NOOP 0x00U
/*! @brief Read command. */
#define BCC_CMD_READ 0x01U
/*! @brief Write command. */
#define BCC_CMD_WRITE 0x02U
/*! @brief Global write command. */
#define BCC_CMD_GLOB_WRITE 0x03U

/*!
 * @brief Returns data field of the communication frame.
 *
 * @param msg Pointer to the frame.
 * @return Data field.
 */
#define BCC_GET_MSG_DATA(msg)                            \
    (((uint16_t) * ((msg) + BCC_MSG_IDX_DATA_H) << 8U) | \
     (uint16_t) * ((msg) + BCC_MSG_IDX_DATA_L))

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

/*! @brief Address of the last register. */
#define BCC_MAX_REG_ADDR 0x7FU

/*! @brief RESET de-glitch filter (t_RESETFLT, typ.) in [us]. */
#define BCC_T_RESETFLT_US 100U

/*! @brief CSB wake-up de-glitch filter, low to high transition
 * (CSB_WU_FLT, max.) in [us].
 * MC33771C: Max 80 us
 * MC33772C: Max 65 us */
#define BCC_CSB_WU_FLT_US 80U

/*! @brief Power up duration (t_WAKE-UP, max.) in [us]. */
#define BCC_T_WAKE_UP_US 440U

/*! @brief Maximal MC33771C fuse mirror address for read access. */
#define MC33771C_MAX_FUSE_READ_ADDR 0x1AU

/*! @brief Maximal MC33771C fuse mirror address for read access. */
#define MC33772C_MAX_FUSE_READ_ADDR 0x12U

/*! @brief Maximal MC33771C fuse mirror address for read access. */
#define MC33771C_MAX_FUSE_WRITE_ADDR 0x17U

/*! @brief Maximal MC33771C fuse mirror address for read access. */
#define MC33772C_MAX_FUSE_WRITE_ADDR 0x0FU

/*! @brief Fuse address of Traceability 0 in MC33771C. */
#define MC33771C_FUSE_TR_0_OFFSET 0x18U

/*! @brief Fuse address of Traceability 1 in MC33771C. */
#define MC33771C_FUSE_TR_1_OFFSET 0x19U

/*! @brief Fuse address of Traceability 2 in MC33771C. */
#define MC33771C_FUSE_TR_2_OFFSET 0x1AU

/*! @brief Fuse address of Traceability 0 in MC33772C. */
#define MC33772C_FUSE_TR_0_OFFSET 0x10U

/*! @brief Fuse address of Traceability 1 in MC33772C. */
#define MC33772C_FUSE_TR_1_OFFSET 0x11U

/*! @brief Fuse address of Traceability 2 in MC33772C. */
#define MC33772C_FUSE_TR_2_OFFSET 0x12U

/*! @brief Mask of Traceability 0 data. */
#define BCC_FUSE_TR_0_MASK 0xFFFFU

/*! @brief Mask of Traceability 1 data. */
#define BCC_FUSE_TR_1_MASK 0xFFFFU

/*! @brief Mask of Traceability 2 data. */
#define BCC_FUSE_TR_2_MASK 0x001FU

/*******************************************************************************
 * Static class variable intialization
 ******************************************************************************/

BCC::can_state_t BCC::mCANstateGlobal = OK;
uint32_t BCC::mLastGlobalConversionStarted = 0;

/*******************************************************************************
 * Constant variables
 ******************************************************************************/

/*! @brief Array containing cell maps for different numbers of cells connected
 * to the MC33771C (0) or MC33771C (1). */
static const uint16_t s_cellMap[2][MC33771C_MAX_CELLS + 1] = {{
                                                                  0x0000, /* Unsupported number of cells. */
                                                                  0x0000, /* Unsupported number of cells. */
                                                                  0x0000, /* Unsupported number of cells. */
                                                                  0x0000, /* Unsupported number of cells. */
                                                                  0x0000, /* Unsupported number of cells. */
                                                                  0x0000, /* Unsupported number of cells. */
                                                                  0x0000, /* Unsupported number of cells. */
                                                                  0x380F, /* 7 cells. */
                                                                  0x3C0F, /* 8 cells. */
                                                                  0x3E0F, /* 9 cells. */
                                                                  0x3F0F, /* 10 cells. */
                                                                  0x3F8F, /* 11 cells. */
                                                                  0x3FCF, /* 12 cells. */
                                                                  0x3FEF, /* 13 cells. */
                                                                  0x3FFF  /* 14 cells. */
                                                              },
                                                              {
                                                                  0x0000, /* Unsupported number of cells. */
                                                                  0x0000, /* Unsupported number of cells. */
                                                                  0x0000, /* Unsupported number of cells. */
                                                                  0x0023, /* 3 cells. */
                                                                  0x0033, /* 4 cells. */
                                                                  0x003B, /* 5 cells. */
                                                                  0x003F, /* 6 cells. */
                                                                  0x0000, /* Unsupported number of cells. */
                                                                  0x0000, /* Unsupported number of cells. */
                                                                  0x0000, /* Unsupported number of cells. */
                                                                  0x0000, /* Unsupported number of cells. */
                                                                  0x0000, /* Unsupported number of cells. */
                                                                  0x0000, /* Unsupported number of cells. */
                                                                  0x0000, /* Unsupported number of cells. */
                                                                  0x0000, /* Unsupported number of cells. */
                                                              }};
/*******************************************************************************
 * PRIVATE FUNCTIONS
 ******************************************************************************/

/*!
 * @brief Returns the channel index for a given cell index
 *
 * @param cellNo    Cell number (range is {0, ..., CONNECTED_CELLS - 1}).
 *
 * @return Channel index
 */
uint8_t BCC::getChannelIndex(const uint8_t cellNo)
{
    if (mDevice == BCC_DEVICE_MC33771C)
    {
        if (cellNo < 4)
        {
            return cellNo;
        }
        else
        {
            return (MC33771C_MAX_CELLS - getCellCount()) + cellNo;
        }
    }
    else
    {
        if (cellNo < 2)
        {
            return cellNo;
        }
        else
        {
            return (MC33772C_MAX_CELLS - getCellCount()) + cellNo;
        }
    }
}

/*!
 * @brief This function configures selected GPIO/AN pin as analog input, digital
 * input or digital output by writing the GPIO_CFG1[GPIOx_CFG] bit field.
 *
 * @param gpioSel   Index of pin to be configured. Index starts at 0 (GPIO0).
 * @param mode      Pin mode. The only accepted enum items are:
 *                  BCC_PIN_ANALOG_IN_RATIO, BCC_PIN_ANALOG_IN_ABS,
 *                  BCC_PIN_DIGITAL_IN and BCC_PIN_DIGITAL_OUT.
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::setGpioCfg(const uint8_t gpioSel, const bcc_pin_mode_t mode)
{
    if ((gpioSel >= BCC_GPIO_INPUT_CNT) || (mode > BCC_PIN_DIGITAL_OUT))
    {
        setCANstate(BCC_STATUS_PARAM_RANGE);
        return BCC_STATUS_PARAM_RANGE;
    }

    /* Update the content of GPIO_CFG1 register. */
    return regUpdate(MC33771C_GPIO_CFG1_OFFSET,
                     (uint16_t)(MC33771C_GPIO_CFG1_GPIO0_CFG_MASK << (gpioSel * 2U)),
                     (uint16_t)(((uint16_t)mode) << (gpioSel * 2U)));
}

/*!
 * @brief This function maps the status to a CAN state
 *
 * @param state    Latest status
 */
BCC::can_state_t BCC::CANstateFromStatus(bcc_status_t status)
{
    switch (status)
    {
    case BCC_STATUS_SUCCESS:
        return OK;
    case BCC_STATUS_PARAM_RANGE:
        return PARAM_ERR;
    case BCC_STATUS_SPI_FAIL:
        return COMM_ERR;
    case BCC_STATUS_COM_TIMEOUT:
        return COMM_ERR;
    case BCC_STATUS_COM_ECHO:
        return COMM_ERR;
    case BCC_STATUS_COM_CRC:
        return COMM_ERR;
    case BCC_STATUS_COM_MSG_CNT:
        return COMM_ERR;
    case BCC_STATUS_COM_NULL:
        return COMM_ERR;
    case BCC_STATUS_DIAG_FAIL:
        return OK;
    case BCC_STATUS_DATA_RDY:
        return DATA_RDY_ERR;
    default:
        return OK;
    }
}

/*!
 * @brief This function updates the CAN state if necessary. The CAN state is a
 * shorted version of the status used to send over CAN in 2 bits
 *
 * @param state    Latest status
 */
void BCC::setCANstate(bcc_status_t status)
{
    auto newCANstate = CANstateFromStatus(status);

    // If the new CAN state has a higher priority that the current state, update it
    if (newCANstate > mCANstate)
    {
        mCANstate = newCANstate;
    }
}

/*!
 * @brief This function updates the CAN state if necessary. The CAN state is a
 * shorted version of the status used to send over CAN in 2 bits
 *
 * @param state    Latest status
 */
void BCC::setCANstateGlobal(bcc_status_t status)
{
    auto newCANstateGlobal = CANstateFromStatus(status);

    // If the new CAN state has a higher priority that the current state, update it
    if (newCANstateGlobal > mCANstateGlobal)
    {
        mCANstateGlobal = newCANstateGlobal;
    }
}

/*******************************************************************************
 * PUBLIC FUNCTIONS
 ******************************************************************************/

BCC::BCC(Config_t config, uint8_t cid) : mDevice(config.DEVICE_TYPE),
                                         mCellCount(config.CELL_COUNT),
                                         mNTCCount(config.NTC_COUNT),
                                         mCurrentSenseEnabled(config.CURRENT_SENSING_ENABLED),
                                         mCID((bcc_cid_t)cid),
                                         mCellMap(s_cellMap[mDevice][mCellCount]),
                                         mAmpHourBackupReg(config.AMPHOUR_BACKUP_REG)
{
    // Set the correct amp hour backup register pointer if valid
    BCC_MCU_Assert(IS_RTC_BKP(mAmpHourBackupReg) && IS_RTC_BKP(mAmpHourBackupReg + 1U));
    mAmpHour = (volatile double *)((&(TAMP->BKP0R)) + mAmpHourBackupReg);
}

/*!
 * @brief This function reads desired number of registers of the BCC device.
 *
 * In case of simultaneous read of more registers, address is incremented
 * in ascending manner.
 *
 * @param regAddr   Register address. See MC33771C.h and MC33772C.h header files
 *                  for possible values (MC3377*C_*_OFFSET macros).
 * @param regCnt    Number of registers to read.
 * @param regVal    Pointer to memory where content of selected 16 bit registers
 *                  will be stored.
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::regRead(const uint8_t regAddr, const uint8_t regCnt, uint16_t *regVal, const bool toUnassigned)
{
    uint8_t txBuf[BCC_MSG_SIZE]; /* TX buffer. */
    uint8_t regIdx;              /* Index of a received register. */
    bcc_status_t status;

    configASSERT(regVal != NULL);

    if ((regAddr > BCC_MAX_REG_ADDR) ||
        (regCnt == 0U) || ((regAddr + regCnt - 1U) > BCC_MAX_REG_ADDR))
    {
        setCANstate(BCC_STATUS_PARAM_RANGE);
        return BCC_STATUS_PARAM_RANGE;
    }

    /* Create frame for request. */
    BCC_Communication::packFrame((uint16_t)regCnt, regAddr, toUnassigned ? BCC_CID_UNASSIG : mCID, BCC_CMD_READ, txBuf);

    status = BCC_Communication::transfer(txBuf, regCnt + 1);
    if (status != BCC_STATUS_SUCCESS)
    {
        setCANstate(status);
        return status;
    }

    /* Check the echo frame. */
    status = BCC_Communication::checkEchoFrame(txBuf);
    if (status != BCC_STATUS_SUCCESS)
    {
        setCANstate(status);
        return status;
    }

    /* Check and store responses. */
    const uint8_t *rxBufMsgIdx = BCC_Communication::getRxBuf();
    for (regIdx = 0U; regIdx < regCnt; regIdx++)
    {
        rxBufMsgIdx += BCC_MSG_SIZE;

        /* Check CRC. */
        if ((status = BCC_Communication::checkCRC(rxBufMsgIdx)) != BCC_STATUS_SUCCESS)
        {
            setCANstate(status);
            return status;
        }

        /* Check the Message counter value. */
        if ((status = checkMsgCnt(rxBufMsgIdx, toUnassigned)) != BCC_STATUS_SUCCESS)
        {
            setCANstate(status);
            return status;
        }

        /* Store data. */
        *regVal++ = BCC_GET_MSG_DATA(rxBufMsgIdx);
    }

    return BCC_STATUS_SUCCESS;
}

/*!
 * @brief This function writes a value to addressed register of the BCC device.
 *
 * @param regAddr   Register address. See MC33771C.h and MC33772C.h header files
 *                  for possible values (MC3377*C_*_OFFSET macros).
 * @param regVal    New value of selected register.
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::regWrite(const uint8_t regAddr, const uint16_t regVal, const bool toUnassigned)
{
    uint8_t txBuf[BCC_MSG_SIZE]; /* Transmission buffer. */
    bcc_status_t status;

    if (regAddr > BCC_MAX_REG_ADDR)
    {
        setCANstate(BCC_STATUS_PARAM_RANGE);
        return BCC_STATUS_PARAM_RANGE;
    }

    /* Create frame for writing. */
    BCC_Communication::packFrame(regVal, regAddr, toUnassigned ? BCC_CID_UNASSIG : mCID, BCC_CMD_WRITE, txBuf);

    status = BCC_Communication::transfer(txBuf, 1);
    if (status != BCC_STATUS_SUCCESS)
    {

        setCANstate(status);
        return status;
    }

    /* Check the echo frame. */
    status = BCC_Communication::checkEchoFrame(txBuf);
    if (status != BCC_STATUS_SUCCESS)
    {
        setCANstate(status);
    }
    return status;
}

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
bcc_status_t BCC::regWriteGlobal(const uint8_t regAddr, const uint16_t regVal)
{
    uint8_t txBuf[BCC_MSG_SIZE]; /* Transmission buffer. */
    bcc_status_t status;

    /* Check input parameters. */
    if (regAddr > BCC_MAX_REG_ADDR)
    {
        setCANstateGlobal(BCC_STATUS_PARAM_RANGE);
        return BCC_STATUS_PARAM_RANGE;
    }

    /* Create frame for writing. */
    BCC_Communication::packFrame(regVal, regAddr, BCC_CID_UNASSIG, BCC_CMD_GLOB_WRITE, txBuf);

    status = BCC_Communication::transfer(txBuf, 1);
    if (status != BCC_STATUS_SUCCESS)
    {
        setCANstateGlobal(status);
        return status;
    }

    /* Check the echo frame. */
    status = BCC_Communication::checkEchoFrame(txBuf);
    if (status != BCC_STATUS_SUCCESS)
    {
        setCANstateGlobal(status);
    }
    return status;
}

/*!
 * @brief This function updates content of a selected register; affects bits
 * specified by a bit mask only.
 *
 * @param regAddr   Register address. See MC33771C.h and MC33772C.h header files
 *                  for possible values (MC3377*C_*_OFFSET macros).
 * @param regMask   Bit mask. Bits set to 1 will be updated.
 * @param regVal    New value of register bits defined by bit mask.
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::regUpdate(const uint8_t regAddr, const uint16_t regMask, const uint16_t regVal)
{
    uint16_t regValTemp;
    bcc_status_t status;

    status = regRead(regAddr, 1U, &regValTemp);
    if (status != BCC_STATUS_SUCCESS)
    {
        return status;
    }

    /* Update register value. */
    regValTemp = regValTemp & ~(regMask);
    regValTemp = regValTemp | (regVal & regMask);

    return regWrite(regAddr, regValTemp);
}

/*!
 * @brief This function sends a No Operation command to the BCC device.
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::sendNop()
{
    uint8_t txBuf[BCC_MSG_SIZE]; /* Transmission buffer. */
    bcc_status_t status;

    /* Create frame for writing.
     * Note: Register Data, Register Address and Message counter fields can
     * contain any value. */
    BCC_Communication::packFrame(0x0000U, 0x00U, mCID, BCC_CMD_NOOP, txBuf);

    status = BCC_Communication::transfer(txBuf, 1);
    if (status != BCC_STATUS_SUCCESS)
    {
        setCANstate(status);
        return status;
    }

    /* Check the echo frame. */
    status = BCC_Communication::checkEchoFrame(txBuf);
    if (status != BCC_STATUS_SUCCESS)
    {
        setCANstate(status);
    }
    return status;
}

/*!
 * @brief This function check the message count of the BCC device.
 *
 * @param resp       Response to check.
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::checkMsgCnt(const uint8_t *const resp, bool toUnassigned)
{
    uint8_t msgCntPrev; /* Previously received message counter value. */
    uint8_t msgCntRcv;  /* Currently received message counter value. */

    configASSERT(resp != NULL);

    msgCntPrev = mMsgCnt;
    msgCntRcv = (resp[BCC_MSG_IDX_CNT_CMD] & BCC_MSG_MSG_CNT_MASK) >> BCC_MSG_MSG_CNT_SHIFT;

    /* Store the Message counter value. */
    mMsgCnt = msgCntRcv;

    /* Check the Message counter value.
     * Note: Do not perform a check for CID=0. */
    if (!toUnassigned && (msgCntRcv != BCC_INC_MSG_CNTR(msgCntPrev)))
    {
        setCANstate(BCC_STATUS_COM_MSG_CNT);
        return BCC_STATUS_COM_MSG_CNT;
    }

    return BCC_STATUS_SUCCESS;
}

/*******************************************************************************
 * PUBLIC FUNCTIONS - API
 ******************************************************************************/

/*!
 * @brief This function assigns CID to a BCC device that has CID equal to zero.
 * It also stores the MsgCntr value for appropriate CID, terminates the RDTX_OUT
 * of the last node if loop-back is not required (in the TPL mode) and checks
 * if the node with newly set CID replies.
 *
 * @param cid       Cluster ID to be set to the BCC device with CID=0.
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::assignCid(uint8_t devicesCnt)
{
    uint16_t writeVal, readVal;
    bcc_status_t status;

    /* Check if unassigned node replies. This is the first reading after device
     * reset. */
    /* Note: In SPI communication mode, the device responds with all bit filed
     * set to zero except message counter and the correct CRC to the very first
     * MCU <-> MC33771C/772C message. */
    status = regRead(MC33771C_INIT_OFFSET, 1U, &readVal, true);
    if (status != BCC_STATUS_SUCCESS)
    {
        PRINTF_WARN("[BCC] AssignCID Failed: no response at CID=0\n");
        PRINTF_ERR("[BCC] assign cid failed at %d\n", mCID);
        return status;
    }

    /* Assign CID;
     * Terminate RDTX_OUT of the last node in TPL setup without loop-back.
     * Stop forwarding only for MC33772C in TPL setup with one node and no
     * loop-back. RDTX_OUT should not be terminated in this case. */
    writeVal = MC33771C_INIT_CID(mCID) |
               MC33771C_INIT_RDTX_IN(MC33771C_INIT_RDTX_IN_DISABLED_ENUM_VAL);

    if ((devicesCnt == 1U) &&
        (mDevice == BCC_DEVICE_MC33772C))
    {
        writeVal |= MC33772C_INIT_TPL2_TX_TERM(MC33772C_INIT_TPL2_TX_TERM_DISABLED_ENUM_VAL) |
                    MC33772C_INIT_BUS_FW(MC33772C_INIT_BUS_FW_DISABLED_ENUM_VAL);
    }
    else if (((uint8_t)mCID == devicesCnt))
    {
        writeVal |= MC33771C_INIT_RDTX_OUT(MC33771C_INIT_RDTX_OUT_ENABLED_ENUM_VAL);

        if (mDevice == BCC_DEVICE_MC33772C)
        {
            writeVal |= MC33772C_INIT_BUS_FW(MC33772C_INIT_BUS_FW_ENABLED_ENUM_VAL);
        }
    }
    else
    {
        writeVal |= MC33771C_INIT_RDTX_OUT(MC33771C_INIT_RDTX_OUT_DISABLED_ENUM_VAL);

        if (mDevice == BCC_DEVICE_MC33772C)
        {
            writeVal |= MC33772C_INIT_BUS_FW(MC33772C_INIT_BUS_FW_ENABLED_ENUM_VAL);
        }
    }

    status = regWrite(MC33771C_INIT_OFFSET, writeVal, true);
    if (status == BCC_STATUS_SUCCESS)
    {

        /* Check if assigned node replies. */
        status = regRead(MC33771C_INIT_OFFSET, 1U, &readVal);

        /* Check the written data. */
        if ((status == BCC_STATUS_SUCCESS) && (writeVal != readVal))
        {
            status = BCC_STATUS_SPI_FAIL;
            PRINTF_WARN("[BCC] AssignCID Failed: readVal != writeVal\n");
            setCANstate(status);
        }
    }
    else
    {
        PRINTF_WARN("[BCC] AssignCID Failed: initial regWrite failed\n");
    }

    if (status != BCC_STATUS_SUCCESS)
    {
        /* Wait and try to assign CID once again. */
        BCC_MCU_WaitUs(750U);

        status = regWrite(MC33771C_INIT_OFFSET, writeVal, true);
        if (status == BCC_STATUS_SUCCESS)
        {

            status = regRead(MC33771C_INIT_OFFSET, 1U, &readVal);

            /* Check the written data. */
            if ((status == BCC_STATUS_SUCCESS) && (writeVal != readVal))
            {
                PRINTF_WARN("[BCC] AssignCID Failed: writeVal != readVal\n");
                status = BCC_STATUS_SPI_FAIL;
                setCANstate(status);
            }
            else if (status != BCC_STATUS_SUCCESS)
            {
                PRINTF_WARN("[BCC] AssignCID Failed: second regRead failed\n");
            }
        }
        else
        {
            PRINTF_WARN("[BCC] AssignCID Failed: second regWrite\n");
        }
    }

    return status;
}

/*!
 * @brief This function resets the BCC device using software reset. It enters reset
 * via SPI or TPL interface.
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::softwareReset()
{
    return regWrite(MC33771C_SYS_CFG1_OFFSET, MC33771C_SYS_CFG1_SOFT_RST(MC33771C_SYS_CFG1_SOFT_RST_ACTIVE_ENUM_VAL));
}

/*!
 * @brief This function resets the coulomb counter of the BCC device.
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::resetBCCCoulombCounter()
{
    return regUpdate(MC33771C_ADC_CFG_OFFSET, MC33771C_ADC_CFG_CC_RST_MASK, MC33771C_ADC_CFG_CC_RST(MC33771C_ADC_CFG_CC_RST_RESET_ENUM_VAL));
}

/*!
 * @brief This function resets the left ampere-hour counter (stored in NVM of uC)
 */
void BCC::setAhCounter(double amphour)
{
    *mAmpHour = amphour;
}

/*!
 * @brief This function starts ADC conversion in selected BCC device. It sets
 * number of samples to be averaged and Start of Conversion bit in ADC_CFG
 * register.
 *
 * You can use function BCC_Meas_IsConverting to check the conversion status or
 * a blocking function BCC_Meas_StartAndWait instead.
 * Measured values can be retrieved e.g. by function BCC_Meas_GetRawValues.
 *
 * @param avg       Number of samples to be averaged.
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::meas_StartConversion(const bcc_avg_t avg)
{
    if (avg > BCC_AVG_256)
    {
        setCANstate(BCC_STATUS_PARAM_RANGE);
        return BCC_STATUS_PARAM_RANGE;
    }

    auto status = regUpdate(MC33771C_ADC_CFG_OFFSET,
                            MC33771C_ADC_CFG_SOC_MASK | MC33771C_ADC_CFG_AVG_MASK,
                            MC33771C_ADC_CFG_SOC(MC33771C_ADC_CFG_SOC_ENABLED_ENUM_VAL) |
                                MC33771C_ADC_CFG_AVG(avg));
    if (status == BCC_STATUS_SUCCESS)
    {
        mLastConversionStarted = micros();
    }
    return status;
}

/*!
 * @brief This function starts ADC conversion for all devices in TPL chain. It
 * uses a Global Write command to set ADC_CFG register.
 *
 * You can use function BCC_Meas_IsConverting to check conversion status and
 * function BCC_Meas_GetRawValues to retrieve the measured values.
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::meas_StartConversionGlobal()
{
    // Determine the value of ADC_CFG register. SOC bit is set to 1, CC_RST bit is set to 0, otherwise use default value.
    const uint16_t adcCfgValue = (MC33771C_ADC_CFG_VALUE & ~MC33771C_ADC_CFG_CC_RST_MASK) | MC33771C_ADC_CFG_SOC_MASK;
    auto status = regWriteGlobal(MC33771C_ADC_CFG_OFFSET, adcCfgValue);
    if (status == BCC_STATUS_SUCCESS)
    {
        mLastGlobalConversionStarted = micros();
    }
    return status;
}

/*!
 * @brief This function checks status of conversion defined by End of Conversion
 * bit in ADC_CFG register.
 *
 * @param completed Pointer to check result. True if a conversion is complete.
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::meas_IsConverting(bool *const completed)
{
    uint16_t adcCfgVal;
    bcc_status_t status;

    BCC_MCU_Assert(completed != NULL);

    status = regRead(MC33771C_ADC_CFG_OFFSET, 1U, &adcCfgVal);

    *(completed) = ((adcCfgVal & MC33771C_ADC_CFG_EOC_N_MASK) ==
                    MC33771C_ADC_CFG_EOC_N(MC33771C_ADC_CFG_EOC_N_COMPLETED_ENUM_VAL));

    return status;
}

/*!
 * @brief This function waits until the bcc is done with the conversion.
 * This is checking using the meas_isConverting() function. This function is blocking,
 * it will return if conversion is done or if timeout has been reached.
 *
 * @author Arjan Blankestijn
 *
 * @param avg Number of samples to be averaged. Timeout increases with number of samples.
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::meas_WaitOnConversion(const bcc_avg_t avg)
{
    bcc_status_t status;
    bool complete = false;

    // Wait for BCC_T_EOC_TIMEOUT_US before checking if the conversion is done.
    // Reduce the waiting time by the time that has already passed since the last conversion started
    // Either use the global or local version of the last conversion started time, depending on which one is more recent
    uint32_t lastConversionStarted = std::max(mLastConversionStarted, mLastGlobalConversionStarted);
    uint32_t timeSinceConvStarted = micros() - lastConversionStarted;
    int32_t timeToWait = ((int32_t)BCC_T_EOC_TIMEOUT_US << ((uint8_t)avg)) - ((int32_t)timeSinceConvStarted);

    if (timeToWait > 0)
    {
        delayUS(timeToWait);
    }

    status = meas_IsConverting(&complete);
    if (status != BCC_STATUS_SUCCESS)
    {
        return status;
    }

    if (!complete)
    {
        PRINTF_WARN("[BCC] WaitOnConversion timeout\n");
        PRINTF_WARN("    Time since last conversion: %lu us\n", micros() - lastConversionStarted);
    }

    return (complete) ? BCC_STATUS_SUCCESS : BCC_STATUS_COM_TIMEOUT;
}

/*!
 * @brief This function starts an on-demand conversion in selected BCC device
 * and waits for completion.
 *
 * @param avg       Number of samples to be averaged.
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::meas_StartAndWait(const bcc_avg_t avg)
{
    bcc_status_t status;

    if (avg > BCC_AVG_256)
    {
        setCANstate(BCC_STATUS_PARAM_RANGE);
        return BCC_STATUS_PARAM_RANGE;
    }

    status = meas_StartConversion(avg);
    if (status != BCC_STATUS_SUCCESS)
    {
        return status;
    }

    return meas_WaitOnConversion(avg);
}

/*!
 * @brief This function reads the measurement registers and returns raw values.
 *
 * Macros defined in bcc_utils.h can be used to perform correct unit conversion.
 *
 * Warning: The "data ready" flag is ignored.
 *
 *
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::meas_GetRawValues()
{
    bcc_status_t status;

    /* Read all the measurement registers.
     * Note: the order and number of registers conforms to the order of measured
     * values in Measurements array, see enumeration bcc_measurements_t. */
    if (mDevice == BCC_DEVICE_MC33771C)
    {
        status = regRead(MC33771C_CC_NB_SAMPLES_OFFSET,
                         BCC_MEAS_CNT, (uint16_t *)&mRawMeasurements);
    }
    else
    {
        status = regRead(MC33772C_CC_NB_SAMPLES_OFFSET,
                         (MC33772C_MEAS_STACK_OFFSET - MC33772C_CC_NB_SAMPLES_OFFSET) + 1, (uint16_t *)&mRawMeasurements);
        if (status != BCC_STATUS_SUCCESS)
        {
            return status;
        }

        /* Skip the reserved registers to speed-up this function. */
        mRawMeasurements[MSR_CELL_VOLT14] = 0x0000;
        mRawMeasurements[MSR_CELL_VOLT13] = 0x0000;
        mRawMeasurements[MSR_CELL_VOLT12] = 0x0000;
        mRawMeasurements[MSR_CELL_VOLT11] = 0x0000;
        mRawMeasurements[MSR_CELL_VOLT10] = 0x0000;
        mRawMeasurements[MSR_CELL_VOLT9] = 0x0000;
        mRawMeasurements[MSR_CELL_VOLT8] = 0x0000;
        mRawMeasurements[MSR_CELL_VOLT7] = 0x0000;

        status = regRead(MC33772C_MEAS_CELL6_OFFSET,
                         (MC33772C_MEAS_VBG_DIAG_ADC1B_OFFSET - MC33772C_MEAS_CELL6_OFFSET) + 1,
                         ((uint16_t *)&mRawMeasurements + ((uint8_t)BCC_MSR_CELL_VOLT6)));
    }

    if (status == BCC_STATUS_SUCCESS)
    {
        timeReceivedLastMeasurement = millis();
    }

    return status;
}

/*!
 * @brief This function reads the Coulomb counter and returns the total Ah left
 * as well as the average current since the last sample.
 *
 * Note that the Coulomb counter is independent on the on-demand conversions.
 *
 * @param rShunt    Shunt resistance [Ohm].
 * @param amphour   Provide pointer to store: Ampere-hour left [Ah].
 * @param Iavg      Provide pointer to store: Average current [A].
 * @param forceRead If True will force an register read command. Else will use previously fetched value
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::meas_GetAmpHourAndIAvg(const double rShunt, const bool invertCurrent, double *const amphour, double *const Iavg, bool forceRead)
{
    bcc_status_t status;

    uint32_t currentTime;
    uint16_t ccSamples;
    int32_t ccAccumulator;

    BCC_MCU_Assert(amphour != NULL);
    BCC_MCU_Assert(Iavg != NULL);
    if (!mCurrentSenseEnabled)
    {
        setCANstate(BCC_STATUS_PARAM_RANGE);
        return BCC_STATUS_PARAM_RANGE;
    }

    if (forceRead)
    {
        // Read the coulomb counter registers
        status = regRead(MC33771C_CC_NB_SAMPLES_OFFSET, 3U, &mRawMeasurements[MSR_CC_NB_SAMPLES]);
        if (status != BCC_STATUS_SUCCESS)
        {
            return status;
        }
    }

    ccSamples = mRawMeasurements[MSR_CC_NB_SAMPLES];
    ccAccumulator = BCC_GET_COULOMB_CNT(mRawMeasurements[MSR_COULOMB_CNT1], mRawMeasurements[MSR_COULOMB_CNT2]);

    // Calculate the sampling frequency of the coulomb counter
    currentTime = micros();
    uint16_t deltaSamples = ccSamples - mCCPrevSamples;

    // If no new sample has arrived since the last run of this function, skip the calculation and return the previous value
    if (deltaSamples == 0)
    {
        *amphour = *mAmpHour;
        *Iavg = mIAvg;
        return BCC_STATUS_SUCCESS;
    }
    uint32_t deltaMicros = currentTime - mCCPrevTime;
    double deltaT = deltaMicros / 1E6f;
    mCCPrevSamples = ccSamples;
    mCCPrevTime = currentTime;

    // If for some reason the deltaT is zero, return an error (prevent divide by zero)
    if (deltaMicros == 0)
    {
        setCANstate(BCC_STATUS_PARAM_RANGE);
        return BCC_STATUS_PARAM_RANGE;
    }
    double ccFreq = (double)deltaSamples / deltaT;

    // Calculate the coulomb count delta from the accumulator delta and the sampling frequency
    int32_t deltaAccumulator = ccAccumulator - mCCPrevAccumulator;
    mCCPrevAccumulator = ccAccumulator;
    double deltaC = deltaAccumulator / ccFreq * 0.6E-6 / rShunt;

    // Invert the current if necessary
    if (invertCurrent)
    {
        deltaC = -deltaC;
    }

    // Also take the average current draw of the slave IC into account (if active)
    if (millis() - getTimeReceivedLastMeasurement() < 500)
    {
        deltaC -= MC33771C_AVG_CURRENT_DRAW * deltaT;
    }

    // Calculate the average current in A
    double iavg = deltaC / deltaT;

    // Reading below this treshold are considered as noise
    if (abs(iavg) < CURRENT_NOISE_THRESHOLD)
    {
        mIAvg = 0;
        *amphour = *mAmpHour;
    }
    else
    {
        mIAvg = iavg;
        // Calculate the used capacity in Ah
        *mAmpHour += deltaC / 3600;
    }
    *Iavg = mIAvg;
    *amphour = *mAmpHour;

    return BCC_STATUS_SUCCESS;
}

/*!
 * @brief This function reads the ISENSE measurement and converts it to [A].
 *
 * @param rShunt Shunt resistance [Ohm].
 * @param isense Pointer to memory where the ISENSE current (in [A]) will
 *               be stored.
 *
 * @param forceRead If True will force an register read command. Else will use previously fetched value
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::meas_GetIsense(const double rShunt, double *const isense, bool forceRead)
{
    bcc_status_t status;
    int32_t isenseVolt;

    BCC_MCU_Assert(isense != NULL);
    if (!mCurrentSenseEnabled)
    {
        setCANstate(BCC_STATUS_PARAM_RANGE);
        return BCC_STATUS_PARAM_RANGE;
    }

    if (forceRead)
    {
        status = regRead(MC33771C_MEAS_ISENSE1_OFFSET, 2U, &mRawMeasurements[MSR_ISENSE1]);
        if (status != BCC_STATUS_SUCCESS)
        {
            return status;
        }
    }

    if ((mRawMeasurements[MSR_ISENSE1] & mRawMeasurements[MSR_ISENSE2] & MC33771C_MEAS_ISENSE1_DATA_RDY_MASK) == 0U)
    {
        setCANstate(BCC_STATUS_DATA_RDY);
        return BCC_STATUS_DATA_RDY;
    }

    isenseVolt = BCC_GET_ISENSE_VOLT(mRawMeasurements[MSR_ISENSE1], mRawMeasurements[MSR_ISENSE2]);
    *isense = (isenseVolt * 1E-6) / rShunt;
    return BCC_STATUS_SUCCESS;
}

/*!
 * @brief This function reads the stack measurement and converts it to [uV].
 *
 * @param stackVolt Pointer to memory where the stack voltage (in [uV]) will
 *                  be stored.
 *
 * @param forceRead If True will force an register read command. Else will use previously fetched value
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::meas_GetStackVoltage(uint32_t *const stackVolt, bool forceRead)
{
    bcc_status_t status;

    if (forceRead)
    {
        status = regRead(MC33771C_MEAS_STACK_OFFSET, 1U, &mRawMeasurements[MSR_STACK_VOLT]);
        if (status != BCC_STATUS_SUCCESS)
        {
            return status;
        }
    }

    if ((mRawMeasurements[MSR_STACK_VOLT] & MC33771C_MEAS_STACK_DATA_RDY_MASK) == 0U)
    {
        setCANstate(BCC_STATUS_DATA_RDY);
        return BCC_STATUS_DATA_RDY;
    }

    *stackVolt = BCC_GET_STACK_VOLT(mRawMeasurements[MSR_STACK_VOLT]);

    return BCC_STATUS_SUCCESS;
}

/*!
 * @brief This function reads the cell measurements and converts them to [uV].
 *
 * @param cellVolt  Pointer to the array or vector where the cell voltages (in [uV]) will
 *                  be stored.
 *                  For Array: stores all cell voltages in ascending order, including unconnected cells
 *                  For Vector: stores only connected cell voltages in ascending order
 *
 * @param forceRead If True will force an register read command. Else will use previously fetched value
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::meas_GetCellVoltages(uint32_t *const cellVolt, bool forceRead)
{
    uint8_t maxCellCnt = BCC_MAX_CELLS_DEV(mDevice);
    vector<uint32_t> cellVoltVec(getCellCount());
    auto status = meas_GetCellVoltages(cellVoltVec, forceRead);
    if (status != BCC_STATUS_SUCCESS)
    {
        return status;
    }

    // PRINTF_INFO("Cell map: 0x%04X\n", getCellMap());
    size_t j = 0;
    for (size_t i = 0; i < maxCellCnt; i++)
    {
        // Check if cell is connected
        if (BCC_IS_CELL_CONN(this, i + 1) == 0)
        {
            // PRINTF_INFO("Cell %u is unconnected\n", i + 1);
            // If cell is unconnected, set the voltage to zero
            cellVolt[i] = 0;
        }
        else
        {
            // Otherwise set the correct voltage read from the vector
            cellVolt[i] = cellVoltVec[j++];
            // PRINTF_INFO("Cell %u is connected, voltage: %.2f\n", i + 1, cellVolt[i]*1e-6);
        }
    }
    return BCC_STATUS_SUCCESS;
}
bcc_status_t BCC::meas_GetCellVoltages(vector<uint32_t> &cellVolt, bool forceRead)
{
    bcc_status_t status;

    uint8_t maxCellCnt, connCellCnt;

    maxCellCnt = BCC_MAX_CELLS_DEV(mDevice);
    uint16_t *const rawVoltages = &mRawMeasurements[MSR_CELL_VOLT14];
    connCellCnt = getCellCount();
    cellVolt.resize(connCellCnt);

    if (forceRead)
    {
        /* Read the measurement registers. */
        status = regRead((mDevice == BCC_DEVICE_MC33771C) ? MC33771C_MEAS_CELL14_OFFSET : MC33771C_MEAS_CELL6_OFFSET,
                         maxCellCnt, rawVoltages);
        if (status != BCC_STATUS_SUCCESS)
        {
            return status;
        }
    }

    // All registers should have the data ready bit set to high
    uint16_t dataReady = 0xffff;

    /* Convert measurements to [uV], change the cell order and check the data-ready flag. */
    size_t j = 0;
    for (size_t cell = 0; cell < maxCellCnt; cell++)
    {
        if (BCC_IS_CELL_CONN(this, cell + 1) == 0)
        {
            continue;
        }
        size_t regIdx = maxCellCnt - (cell + 1);
        cellVolt[j++] = BCC_GET_VOLT(rawVoltages[regIdx]);
        dataReady &= rawVoltages[regIdx];
    }

    // Compare dataReady with mask
    if ((dataReady & MC33771C_MEAS_CELL1_DATA_RDY_MASK) == 0U)
    {
        setCANstate(BCC_STATUS_DATA_RDY);
        return BCC_STATUS_DATA_RDY;
    }

    return BCC_STATUS_SUCCESS;
}

/*!
 * @brief This function reads the voltage measurement of a selected cell and
 * converts it to [uV].
 *
 * @param cellIndex Cell index. Use 0U for CELL 1, 1U for CELL2, etc.
 * @param cellVolt  Pointer to memory where the cell voltage (in [uV]) will
 *                  be stored.
 *
 * @param forceRead If True will force an register read command. Else will use previously fetched value
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::meas_GetCellVoltage(uint8_t cellIndex, uint32_t *const cellVolt, bool forceRead)
{
    bcc_status_t status;

    BCC_MCU_Assert(cellVolt != NULL);

    uint16_t *const cellVoltage = &mRawMeasurements[MSR_CELL_VOLT14 + cellIndex];

    if (cellIndex >= BCC_MAX_CELLS_DEV(mDevice))
    {
        setCANstate(BCC_STATUS_PARAM_RANGE);
        return BCC_STATUS_PARAM_RANGE;
    }

    if (forceRead)
    {
        status = regRead(MC33771C_MEAS_CELL1_OFFSET - cellIndex, 1U, cellVoltage);
        if (status != BCC_STATUS_SUCCESS)
        {
            return status;
        }
    }

    if ((*cellVoltage & MC33771C_MEAS_CELL1_DATA_RDY_MASK) == 0U)
    {
        setCANstate(BCC_STATUS_DATA_RDY);
        return BCC_STATUS_DATA_RDY;
    }

    *cellVolt = BCC_GET_VOLT(*cellVoltage);

    return BCC_STATUS_SUCCESS;
}

/*!
 * @brief This function reads the voltage measurement for all ANx and converts
 * them to [uV] in absolute mode or in 0-0x7FFF in ratiometric mode.
 *
 * @param anVolt    Pointer to the array where ANx voltages will
 *                  be stored. Array must have an suitable size
 *                  (BCC_GPIO_INPUT_CNT * 32b). AN0 voltage is stored at [0],
 *                  AN1 at [1], etc.
 *
 * @param absVoltage If True will return absolute voltage. Else will return ratiometric voltage
 * @param forceRead If True will force an register read command. Else will use previously fetched value
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::meas_GetAnVoltages(uint32_t *const anVolt, bool absVoltage, bool forceRead)
{
    bcc_status_t status;
    uint16_t *const anVoltages = mRawMeasurements + MSR_AN6;

    BCC_MCU_Assert(anVolt != NULL);

    if (forceRead)
    {
        /* Read the measurement registers. */
        status = regRead(MC33771C_MEAS_AN6_OFFSET,
                         BCC_GPIO_INPUT_CNT, anVoltages);
        if (status != BCC_STATUS_SUCCESS)
        {
            return status;
        }
    }

    uint16_t dataReady = MC33771C_MEAS_AN0_DATA_RDY_MASK;
    /* Convert measurements to [uV] and check the data-ready flag. */
    for (uint8_t i = 0; i < BCC_GPIO_INPUT_CNT; i++)
    {
        if (absVoltage)
        {
            anVolt[i] = BCC_GET_VOLT(anVoltages[BCC_GPIO_INPUT_CNT - (i + 1)]);
        }
        else
        {
            anVolt[i] = BCC_GET_MEAS_RAW(anVoltages[BCC_GPIO_INPUT_CNT - (i + 1)]);
        }
        dataReady &= anVoltages[i];
    }

    if (dataReady == 0U)
    {
        setCANstate(BCC_STATUS_DATA_RDY);
        return BCC_STATUS_DATA_RDY;
    }

    return BCC_STATUS_SUCCESS;
}

/*!
 * @brief This function reads the voltage measurement of a selected ANx and
 * converts them to [uV] in absolute mode or in 0-0x7FFF in ratiometric mode.
 *
 * @param anIndex   ANx index. Use 0U for AN0, 1U for AN1, etc.
 * @param anVolt    Pointer to memory where the ANx voltage will
 *                  be stored.
 *
 * @param absVoltage If True will return absolute voltage. Else will return ratiometric voltage
 * @param forceRead If True will force an register read command. Else will use previously fetched value
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::meas_GetAnVoltage(uint8_t anIndex, uint32_t *const anVolt, bool absVoltage, bool forceRead)
{
    bcc_status_t status;
    uint16_t *const anVoltage = mRawMeasurements + MSR_AN6 + anIndex;

    BCC_MCU_Assert(anVolt != NULL);

    if (anIndex >= BCC_GPIO_INPUT_CNT)
    {
        setCANstate(BCC_STATUS_PARAM_RANGE);
        return BCC_STATUS_PARAM_RANGE;
    }

    if (forceRead)
    {
        status = regRead(MC33771C_MEAS_AN0_OFFSET - anIndex, 1U, anVoltage);
        if (status != BCC_STATUS_SUCCESS)
        {
            return status;
        }
    }

    if ((*anVoltage & MC33771C_MEAS_AN0_DATA_RDY_MASK) == 0U)
    {
        setCANstate(BCC_STATUS_DATA_RDY);
        return BCC_STATUS_DATA_RDY;
    }

    if (absVoltage)
    {
        *anVolt = BCC_GET_VOLT(*anVoltage);
    }
    else
    {
        *anVolt = BCC_GET_MEAS_RAW(*anVoltage);
    }

    return BCC_STATUS_SUCCESS;
}

/*!
 * @brief This function reads the internal BCC temperature
 *
 * @param icTemp    Pointer to memory where the IC temperature will be stored.
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::meas_GetIcTemperature(uint16_t *const icTemp, bool forceRead)
{
    bcc_status_t status;

    BCC_MCU_Assert(icTemp != NULL);

    if (forceRead)
    {
        if ((status = regRead(MC33771C_MEAS_IC_TEMP_OFFSET, 1U, &mRawMeasurements[MSR_ICTEMP])) != BCC_STATUS_SUCCESS)
        {
            return status;
        }
    }

    if ((mRawMeasurements[MSR_ICTEMP] & MC33771C_MEAS_IC_TEMP_DATA_RDY_MASK) == 0U)
    {
        setCANstate(BCC_STATUS_DATA_RDY);
        return BCC_STATUS_DATA_RDY;
    }

    *icTemp = BCC_GET_IC_TEMP(mRawMeasurements[MSR_ICTEMP]);

    return BCC_STATUS_SUCCESS;
}

/*!
 * @brief This function reads the NTC's temperature
 *
 * @param temperatures    Pointer to vector where the temperatures will be stored.
 * @param NTCresistance   Resistance of the NTC in Ohms
 * @param NTCBeta         Beta value of the NTC
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::meas_GetNTCTemperatures(vector<uint16_t> &temperatures, double NTCresistance, double NTCBeta, bool forceRead)
{
    bcc_status_t status;
    uint32_t ntcVolt[BCC_GPIO_INPUT_CNT];

    temperatures.resize(getNTCCount());
    if ((status = meas_GetAnVoltages(ntcVolt, false, forceRead)) != BCC_STATUS_SUCCESS)
    {
        PRINTF_ERR("Failed to read NTC voltages, error %u\n", status);
        return status;
    }

    // Only convert the connected NTC's, assuming that NTC's are connected from GPIO0 to NTCCount
    for (uint8_t i = 0; i < getNTCCount(); i++)
    {
        temperatures[i] = BCC_VOLT_TO_TEMPRAW(ntcVolt[i], NTCresistance, NTCBeta);
    }

    return BCC_STATUS_SUCCESS;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : applyFaultMasks
 * Description   : Not all faults from FAULT1, FAULT2, FAULT3_STATUS are useful. This mask will remove useless faults
 *
 *END**************************************************************************/
void BCC::applyFaultMasks(uint16_t *faultValues)
{
    // TODO unit test this function

    uint16_t mask = MC33771C_FAULT1_STATUS_CT_UV_FLT_MASK | // All Under-OVer thresholds are already check trough dedicated registers
                    MC33771C_FAULT1_STATUS_CT_OV_FLT_MASK |
                    MC33771C_FAULT1_STATUS_AN_UT_FLT_MASK |
                    MC33771C_FAULT1_STATUS_AN_OT_FLT_MASK |
                    MC33771C_FAULT1_STATUS_I2C_ERR_FLT_MASK | // I2C eeprom is not enabled
                    MC33771C_FAULT1_STATUS_COM_LOSS_FLT_MASK; // For now we ignore this fault. If communication is lost for a long time the master will act approapriately.

    faultValues[BCC_FS_FAULT1] &= ~mask;

    mask = MC33771C_FAULT2_STATUS_IC_TSD_FLT_MASK; // We have a seperate fault for this one

    faultValues[BCC_FS_FAULT2] &= ~mask;

    mask = 0xBFFF; // Masks all End of time cell balancing notifications and Coulomb counter overflow. Bit 15 is not masked

    faultValues[BCC_FS_FAULT3] &= ~mask;
}

/*!
 * @brief This function reads the fault status registers of the BCC device. Will clear the fault status registers.
 *
 * @param allFaults Pointer to the array where the fault status will be stored. Will bitwise-OR all fault status registers with allFaults.
 */
void BCC::fault_GetStatus(uint16_t *const allFaults)
{
    bcc_status_t status;

    BCC_MCU_Assert(mFaultStatusRegisters != NULL);

    /* Read CELL_OV_FLT and CELL_UV_FLT. */
    status = regRead(MC33771C_CELL_OV_FLT_OFFSET, 2U, &mFaultStatusRegisters[BCC_FS_CELL_OV]);
    if (status != BCC_STATUS_SUCCESS)
    {
        return;
    }

    /* Read CB_OPEN_FLT, CB_SHORT_FLT. */
    status = regRead(MC33771C_CB_OPEN_FLT_OFFSET, 2U, &mFaultStatusRegisters[BCC_FS_CB_OPEN]);
    if (status != BCC_STATUS_SUCCESS)
    {
        return;
    }

    /* Read AN_OT_UT_FLT, GPIO_SHORT_Anx_OPEN_STS. */
    status = regRead(MC33771C_AN_OT_UT_FLT_OFFSET, 2U, &mFaultStatusRegisters[BCC_FS_AN_OT_UT]);
    if (status != BCC_STATUS_SUCCESS)
    {
        return;
    }
    uint16_t currentCommErrors = mFaultStatusRegisters[BCC_FS_COMM];
    /* Read COM_STATUS, FAULT1_STATUS, FAULT2_STATUS and FAULT3_STATUS. */
    status = regRead(MC33771C_COM_STATUS_OFFSET, 4U, &mFaultStatusRegisters[BCC_FS_COMM]);
    if (status != BCC_STATUS_SUCCESS)
    {
        return;
    }

    // Be carefull! Apply the fault mask will modify the data in combinedFaults, if you want to log these register. Do this before applying this mask or adapt the mask
    applyFaultMasks(mFaultStatusRegisters);

    // This function is probably not the best place to keep track of whether the comms error count has been increased
    // But it is the easiest way to do so
    mCommunicationErrorCountIncreased = currentCommErrors != mFaultStatusRegisters[BCC_FS_COMM];
    timeReceivedLastMeasurement = millis();

    for (uint8_t i = 0; i < BCC_STAT_CNT; i++)
    {

        allFaults[i] |= mFaultStatusRegisters[i];

        if (mFaultStatusRegisters[i] != 0)
        {
            // Clear the fault on the slave
            fault_ClearStatus((bcc_fault_status_t)i);

            // Print faults
            switch (i)
            {
            case BCC_FS_COMM:
                PRINTF_ERR("[BCC] CID %u: Fault: COM_STATUS, content: 0x%04X\n", mCID, mFaultStatusRegisters[i]);
                break;
            case BCC_FS_FAULT1:
                PRINTF_ERR("[BCC] CID %u: Fault: FAULT1_STATUS, content: 0x%04X\n", mCID, mFaultStatusRegisters[i]);
                break;
            case BCC_FS_FAULT2:
                PRINTF_ERR("[BCC] CID %u: Fault: FAULT2_STATUS, content: 0x%04X\n", mCID, mFaultStatusRegisters[i]);
                break;
            case BCC_FS_FAULT3:
                PRINTF_ERR("[BCC] CID %u: Fault: FAULT3_STATUS, content: 0x%04X\n", mCID, mFaultStatusRegisters[i]);
                break;
            default:
                PRINTF_ERR("[BCC] CID %u: Fault: index %u, content: 0x%04X\n", mCID, i, mFaultStatusRegisters[i]);
                break;
            }
        }
    }
}

/*!
 * @brief This function clears selected fault status register.
 *
 * @param statSel   Selection of a fault status register to be cleared. See
 *                  definition of this enumeration in this header file.
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::fault_ClearStatus(const bcc_fault_status_t statSel)
{
    /* This array is intended for conversion of bcc_fault_status_t value to
     * a BCC register address. */
    const uint8_t regAddrMap[BCC_STAT_CNT] = {
        MC33771C_CELL_OV_FLT_OFFSET, MC33771C_CELL_UV_FLT_OFFSET,
        MC33771C_CB_OPEN_FLT_OFFSET, MC33771C_CB_SHORT_FLT_OFFSET, MC33771C_AN_OT_UT_FLT_OFFSET,
        MC33771C_GPIO_SHORT_ANX_OPEN_STS_OFFSET, MC33771C_COM_STATUS_OFFSET,
        MC33771C_FAULT1_STATUS_OFFSET, MC33771C_FAULT2_STATUS_OFFSET,
        MC33771C_FAULT3_STATUS_OFFSET};

    if ((uint32_t)statSel >= BCC_STAT_CNT)
    {
        setCANstate(BCC_STATUS_PARAM_RANGE);
        return BCC_STATUS_PARAM_RANGE;
    }

    return regWrite(regAddrMap[statSel], 0x0000U);
}

/*!
 * @brief This function sets the mode of one BCC GPIOx/ANx pin.
 *
 * @param gpioSel   Index of pin to be configured. Index starts at 0 (GPIO0).
 * @param mode      Pin mode.
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::GPIO_SetMode(const uint8_t gpioSel, const bcc_pin_mode_t mode)
{
    bcc_status_t status = BCC_STATUS_PARAM_RANGE;

    if (gpioSel >= BCC_GPIO_INPUT_CNT)
    {
        setCANstate(BCC_STATUS_PARAM_RANGE);
        return BCC_STATUS_PARAM_RANGE;
    }

    if ((mode == BCC_PIN_WAKE_UP_IN) && (gpioSel == 0U))
    {
        /* Set GPIO0 to digital input and enable the wake-up capability. */
        status = setGpioCfg(0U, BCC_PIN_DIGITAL_IN);
        if (status == BCC_STATUS_SUCCESS)
        {
            status = regUpdate(MC33771C_GPIO_CFG2_OFFSET,
                               MC33771C_GPIO_CFG2_GPIO0_WU_MASK,
                               MC33771C_GPIO_CFG2_GPIO0_WU(MC33771C_GPIO_CFG2_GPIO0_WU_WAKEUP_ENUM_VAL));
        }
    }
    else if ((mode == BCC_PIN_CONVERT_TR_IN) && (gpioSel == 2U))
    {
        /* Set GPIO2 to digital input serving as a conversion trigger. */
        status = setGpioCfg(2U, BCC_PIN_DIGITAL_IN);
        if (status == BCC_STATUS_SUCCESS)
        {
            status = regUpdate(MC33771C_GPIO_CFG2_OFFSET,
                               MC33771C_GPIO_CFG2_GPIO2_SOC_MASK,
                               MC33771C_GPIO_CFG2_GPIO2_SOC(MC33771C_GPIO_CFG2_GPIO2_SOC_ADC_TRG_ENABLED_ENUM_VAL));
        }
    }
    else if (mode <= BCC_PIN_DIGITAL_OUT)
    {
        status = BCC_STATUS_SUCCESS;
        if (gpioSel == 0U)
        {
            /* Disable the wake-up capability. */
            status = regUpdate(MC33771C_GPIO_CFG2_OFFSET,
                               MC33771C_GPIO_CFG2_GPIO0_WU_MASK,
                               MC33771C_GPIO_CFG2_GPIO0_WU(MC33771C_GPIO_CFG2_GPIO0_WU_NO_WAKEUP_ENUM_VAL));
        }
        else if (gpioSel == 2U)
        {
            /* Disable the conversion trigger. */
            status = regUpdate(MC33771C_GPIO_CFG2_OFFSET,
                               MC33771C_GPIO_CFG2_GPIO2_SOC_MASK,
                               MC33771C_GPIO_CFG2_GPIO2_SOC(MC33771C_GPIO_CFG2_GPIO2_SOC_ADC_TRG_DISABLED_ENUM_VAL));
        }

        if (status == BCC_STATUS_SUCCESS)
        {
            status = setGpioCfg(gpioSel, mode);
        }
    }

    return status;
}

/*!
 * @brief This function reads a value of one BCC GPIO pin.
 *
 * Note that such GPIO/AN pin should be configured as digital input or output.
 *
 * @param gpioSel   Index of GPIOx to be read. Index starts at 0 (GPIO0).
 * @param val       Pointer where the pin value will be stored. Possible values
 *                  are: False (logic 0, low level) and True (logic 1, high
 *                  level).
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::GPIO_ReadPin(const uint8_t gpioSel, bool *const val)
{
    bcc_status_t status;
    uint16_t gpioStsVal;

    BCC_MCU_Assert(val != NULL);

    if (gpioSel >= BCC_GPIO_INPUT_CNT)
    {
        setCANstate(BCC_STATUS_PARAM_RANGE);
        return BCC_STATUS_PARAM_RANGE;
    }

    /* Read and update content of GPIO_CFG2 register. */
    status = regRead(MC33771C_GPIO_STS_OFFSET, 1U, &gpioStsVal);
    *val = (gpioStsVal & (1U << gpioSel)) > 0U;

    return status;
}

/*!
 * @brief This function sets output value of one BCC GPIO pin.
 *
 * Note that this function is determined for GPIO/AN pins configured as output
 * pins.
 *
 * @param gpioSel   Index of GPIO output to be set. Index starts at 0 (GPIO0).
 * @param val       Output value. Possible values are: False (logic 0, low
 *                  level) and True (logic 1, high level).
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::GPIO_SetOutput(const uint8_t gpioSel, const bool val)
{
    if (gpioSel >= BCC_GPIO_INPUT_CNT)
    {
        setCANstate(BCC_STATUS_PARAM_RANGE);
        return BCC_STATUS_PARAM_RANGE;
    }

    /* Update the content of GPIO_CFG2 register. */
    return regUpdate(MC33771C_GPIO_CFG2_OFFSET,
                     (uint16_t)(1U << gpioSel),
                     (uint16_t)((val ? 1U : 0U) << gpioSel));
}

/*!
 * @brief This function enables or disables the cell balancing via
 * SYS_CFG1[CB_DRVEN] bit.
 *
 * Note that each cell balancing driver needs to be setup separately, e.g. by
 * CB_SetIndividualCell function.
 *
 * @param enable    State of cell balancing. False (cell balancing disabled) or
 *                  True (cell balancing enabled).
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::CB_Enable(const bool enable)
{
    // If cell balancing is to be disabled, reset the timers
    if (!enable)
    {
        memset(mCBTimerEnd, 0, sizeof(mCBTimerEnd));
    }
    return regUpdate(MC33771C_SYS_CFG1_OFFSET, MC33771C_SYS_CFG1_CB_DRVEN_MASK,
                     enable ? MC33771C_SYS_CFG1_CB_DRVEN(MC33771C_SYS_CFG1_CB_DRVEN_ENABLED_ENUM_VAL)
                            : MC33771C_SYS_CFG1_CB_DRVEN(MC33771C_SYS_CFG1_CB_DRVEN_DISABLED_ENUM_VAL));
}

/*!
 * @brief This function enables or disables cell balancing for a specified cell
 * and sets its timer.
 *
 * @param cellIndex Cell index. Use 0U for CELL 1, 1U for CELL2, etc.
 * @param enable    True for enabling of CB, False otherwise.
 * @param timer     Timer for enabled CB driver in minutes. Value of zero
 *                  represents 30 seconds.
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::CB_SetIndividualChannel(const uint8_t channelIndex, const bool enable, const uint16_t timer)
{
    uint16_t cbxCfgVal;
    bcc_status_t status;
    uint16_t timerSeconds = (timer == 0) ? 30 : timer * 60;

    // Parameter checks
    if (channelIndex >= BCC_MAX_CELLS_DEV(mDevice))
    {
        setCANstate(BCC_STATUS_PARAM_RANGE);
        return BCC_STATUS_PARAM_RANGE;
    }

    if (timer > MC33771C_CB1_CFG_CB_TIMER_MASK)
    {
        setCANstate(BCC_STATUS_PARAM_RANGE);
        return BCC_STATUS_PARAM_RANGE;
    }

    // Check if the balancing needs to be enabled or disabled
    if (!isChannelBalancing(channelIndex) && enable)
    {
        // Cell is not being balanced, start balancing
        // PRINTF_INFO("[BCC] CID %u: CB enabled for channel %u\n", mCID, channelIndex + 1);
        cbxCfgVal = MC33771C_CB1_CFG_CB_EN(MC33771C_CB1_CFG_CB_EN_ENABLED_ENUM_VAL);
        cbxCfgVal |= MC33771C_CB1_CFG_CB_TIMER(timer);
        status = regWrite(MC33771C_CB1_CFG_OFFSET + channelIndex, cbxCfgVal);
        if (status != BCC_STATUS_SUCCESS)
        {
            return status;
        }
        mCBTimerEnd[channelIndex] = seconds() + timerSeconds;
    }
    else if (isChannelBalancing(channelIndex) && !enable)
    {
        // Cell is being balanced, stop balancing
        // PRINTF_INFO("[BCC] CID %u: CB disabled for channel %u\n", mCID, channelIndex + 1);
        cbxCfgVal = MC33771C_CB1_CFG_CB_EN(MC33771C_CB1_CFG_CB_EN_DISABLED_ENUM_VAL);
        cbxCfgVal |= MC33771C_CB1_CFG_CB_TIMER(0);
        status = regWrite(MC33771C_CB1_CFG_OFFSET + channelIndex, cbxCfgVal);
        if (status != BCC_STATUS_SUCCESS)
        {
            return status;
        }
        mCBTimerEnd[channelIndex] = 0;
    }
    return BCC_STATUS_SUCCESS;
}

/*!
 * @brief This function enables or disables cell balancing for a specified cell
 * (0-CONNECTED_CELLS) and sets its timer.
 *
 * @param cellIndex Cell index. Use 0U for CELL 1, 1U for CELL2, etc.
 * @param enable    True for enabling of CB, False otherwise.
 * @param timer     Timer for enabled CB driver in minutes. Value of zero
 *                  represents 30 seconds.
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::CB_SetIndividualCell(const uint8_t cellIndex, const bool enable, const uint16_t timer)
{
    return CB_SetIndividualChannel(getChannelIndex(cellIndex), enable, timer);
}

/*!
 * @brief This function pauses cell balancing. It can be useful during an on
 * demand conversion. As a result more precise measurement can be done. Note
 * that it is user obligation to re-enable cell balancing after measurement
 * ends.
 *
 * @param pause     True (pause) or False (unpause).
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::CB_Pause(const bool pause)
{
    return regUpdate(MC33771C_SYS_CFG1_OFFSET, MC33771C_SYS_CFG1_CB_MANUAL_PAUSE_MASK,
                     (pause) ? MC33771C_SYS_CFG1_CB_MANUAL_PAUSE(MC33771C_SYS_CFG1_CB_MANUAL_PAUSE_ENABLED_ENUM_VAL)
                             : MC33771C_SYS_CFG1_CB_MANUAL_PAUSE(MC33771C_SYS_CFG1_CB_MANUAL_PAUSE_DISABLED_ENUM_VAL));
}

/*!
 * @brief This function reads a fuse mirror register of selected BCC device.
 *
 * @param fuseAddr  Address of a fuse mirror register to be read.
 * @param value     Pointer to memory where the read value will be stored.
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::fuseMirror_Read(const uint8_t fuseAddr, uint16_t *const value)
{
    bcc_status_t status;

    BCC_MCU_Assert(value != NULL);

    if (fuseAddr > ((mDevice == BCC_DEVICE_MC33771C) ? MC33771C_MAX_FUSE_READ_ADDR : MC33772C_MAX_FUSE_READ_ADDR))
    {
        setCANstate(BCC_STATUS_PARAM_RANGE);
        return BCC_STATUS_PARAM_RANGE;
    }

    status = regWrite(MC33771C_FUSE_MIRROR_CNTL_OFFSET,
                      MC33771C_FUSE_MIRROR_CNTL_FMR_ADDR(fuseAddr) |
                          MC33771C_FUSE_MIRROR_CNTL_FSTM(MC33771C_FUSE_MIRROR_CNTL_FSTM_LOCKED_ENUM_VAL) |
                          MC33771C_FUSE_MIRROR_CNTL_FST(MC33771C_FUSE_MIRROR_CNTL_FST_SPI_WRITE_ENABLE_ENUM_VAL));
    if (status != BCC_STATUS_SUCCESS)
    {
        return status;
    }

    return regRead(MC33771C_FUSE_MIRROR_DATA_OFFSET, 1U, value);
}

/*!
 * @brief This function writes a fuse mirror register of selected BCC device.
 *
 * @param fuseAddr  Address of a fuse mirror register to be written.
 * @param value     Value to be written.
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::fuseMirror_Write(const uint8_t fuseAddr, const uint16_t value)
{
    bcc_status_t status;

    if (fuseAddr > ((mDevice == BCC_DEVICE_MC33771C) ? MC33771C_MAX_FUSE_WRITE_ADDR : MC33772C_MAX_FUSE_WRITE_ADDR))
    {
        setCANstate(BCC_STATUS_PARAM_RANGE);
        return BCC_STATUS_PARAM_RANGE;
    }

    /* FUSE_MIRROR_CNTL to enable writing. */
    status = regWrite(MC33771C_FUSE_MIRROR_CNTL_OFFSET,
                      MC33771C_FUSE_MIRROR_CNTL_FMR_ADDR(0U) |
                          MC33771C_FUSE_MIRROR_CNTL_FSTM(MC33771C_FUSE_MIRROR_CNTL_FSTM_UNLOCKED_ENUM_VAL) |
                          MC33771C_FUSE_MIRROR_CNTL_FST(MC33771C_FUSE_MIRROR_CNTL_FST_SPI_WRITE_ENABLE_ENUM_VAL));
    if (status != BCC_STATUS_SUCCESS)
    {
        return status;
    }

    /* Send the fuse address. */
    status = regWrite(MC33771C_FUSE_MIRROR_CNTL_OFFSET,
                      MC33771C_FUSE_MIRROR_CNTL_FMR_ADDR(fuseAddr) |
                          MC33771C_FUSE_MIRROR_CNTL_FSTM(MC33771C_FUSE_MIRROR_CNTL_FSTM_UNLOCKED_ENUM_VAL) |
                          MC33771C_FUSE_MIRROR_CNTL_FST(MC33771C_FUSE_MIRROR_CNTL_FST_SPI_WRITE_ENABLE_ENUM_VAL));
    if (status != BCC_STATUS_SUCCESS)
    {
        return status;
    }

    status = regWrite(MC33771C_FUSE_MIRROR_DATA_OFFSET, value);
    if (status != BCC_STATUS_SUCCESS)
    {
        return status;
    }

    /* FUSE_MIRROR_CNTL to low power. */
    return regWrite(MC33771C_FUSE_MIRROR_CNTL_OFFSET,
                    MC33771C_FUSE_MIRROR_CNTL_FMR_ADDR(0U) |
                        MC33771C_FUSE_MIRROR_CNTL_FSTM(MC33771C_FUSE_MIRROR_CNTL_FSTM_UNLOCKED_ENUM_VAL) |
                        MC33771C_FUSE_MIRROR_CNTL_FST(MC33771C_FUSE_MIRROR_CNTL_FST_LP_ENUM_VAL));
}

/*!
 * @brief This function reads an unique serial number of the BCC device from the
 * content of mirror registers.
 *
 * GUID is created according to the following table:
 * |    Device    | GUID [36:21] | GUID [20:5] | GUID [4:0] |
 * |:------------:|:------------:|:-----------:|:----------:|
 * |   MC33771C   | 0x18 [15:0]  | 0x19 [15:0] | 0x1A [4:0] |
 * | fuse address |  (16 bit)    |  (16 bit)   |  (5 bit)   |
 * |:------------:|:------------:|:-----------:|:----------:|
 * |   MC33772C   | 0x10 [15:0]  | 0x11 [15:0] | 0x12 [4:0] |
 * | fuse address |  (16 bit)    |  (16 bit)   |  (5 bit)   |
 *
 * @param guid      Pointer to memory where 37b unique ID will be stored.
 *
 * @return bcc_status_t Error code.
 */
bcc_status_t BCC::GUID_Read(uint64_t *const guid)
{
    const uint8_t addr771c[3] = {
        MC33771C_FUSE_TR_0_OFFSET,
        MC33771C_FUSE_TR_1_OFFSET,
        MC33771C_FUSE_TR_2_OFFSET};
    const uint8_t addr772c[3] = {
        MC33772C_FUSE_TR_0_OFFSET,
        MC33772C_FUSE_TR_1_OFFSET,
        MC33772C_FUSE_TR_2_OFFSET};
    uint8_t const *readAddr;
    uint16_t readData[3];
    uint8_t i;
    bcc_status_t status;

    BCC_MCU_Assert(guid != NULL);

    readAddr = (mDevice == BCC_DEVICE_MC33771C) ? addr771c : addr772c;

    for (i = 0; i < 3; i++)
    {
        status = fuseMirror_Read(readAddr[i], &(readData[i]));
        if (status != BCC_STATUS_SUCCESS)
        {
            return status;
        }
    }

    *guid = (((uint64_t)(readData[0] & BCC_FUSE_TR_0_MASK)) << 21) |
            (((uint64_t)(readData[1] & BCC_FUSE_TR_1_MASK)) << 5) |
            ((uint64_t)(readData[2] & BCC_FUSE_TR_2_MASK));

    return BCC_STATUS_SUCCESS;
}

/**
 * Get the CAN state of the BCC. Also resets the state to OK.
 */
BCC::can_state_t BCC::getCANstate()
{
    auto currentCANstate = mCANstate;
    mCANstate = OK;
    return currentCANstate;
}

/**
 * Get the CAN state of global-level BCC functions. Also resets the state to OK.
 */
BCC::can_state_t BCC::getCANstateGlobal()
{
    auto currentCANstateGlobal = mCANstateGlobal;
    mCANstateGlobal = OK;
    return currentCANstateGlobal;
}

/**
 * Check if the BCC has a valid configuration
 */
bool BCC::hasValidConfig()
{
    if ((mDevice != BCC_DEVICE_MC33771C && mDevice != BCC_DEVICE_MC33772C) || mNTCCount > 7)
    {
        PRINTF_ERR("[BCC] CONFIG ERR: Incorrect device or number of NTCs. Device: %u, Cells: %u, NTCs: %u\n", mDevice, mCellCount, mNTCCount);
        return false;
    }
    if (((mDevice == BCC_DEVICE_MC33771C) && !BCC_IS_IN_RANGE(mCellCount, MC33771C_MIN_CELLS, MC33771C_MAX_CELLS)) ||
        ((mDevice == BCC_DEVICE_MC33772C) && !BCC_IS_IN_RANGE(mCellCount, MC33772C_MIN_CELLS, MC33772C_MAX_CELLS)))
    {
        PRINTF_ERR("[BCC] CONFIG ERR: Incorrect number of cells. Device: %u, Cells: %u, NTCs: %u\n", mDevice, mCellCount, mNTCCount);
        return false;
    }
    return true;
}

const vector<bool> BCC::getBalancingList()
{
    updateBalancing();
    std::vector<bool> balancingList(getCellCount());
    size_t j = 0;
    for (uint8_t i = 0; i < BCC_MAX_CELLS_DEV(mDevice); i++)
    {
        if (BCC_IS_CELL_CONN(this, i + 1))
        {
            balancingList.at(j++) = mBalancingList[i];
        }
    }
    return balancingList;
}

void BCC::updateBalancing()
{
    mAnyCellBalancing = false;
    for (uint8_t i = 0; i < BCC_MAX_CELLS_DEV(mDevice); i++)
    {
        if (mCBTimerEnd[i] > seconds())
        {
            mAnyCellBalancing = true;
            mBalancingList[i] = true;
        }
        else
        {
            mBalancingList[i] = false;
        }
    }
}

/**
 * Returns true if balancing is on for the specified cell. False if off.
 * To check if any cell is balancing, pass -1 as the cellIndex (default)
 */
bool BCC::isChannelBalancing(const int8_t channelIndex)
{
    // First update the balancing list
    updateBalancing();

    // Check if any cell is balancing
    if (channelIndex < 0)
    {
        return mAnyCellBalancing;
    }

    // Check if the cell is balancing
    return mBalancingList[channelIndex];
}

/**
 * Returns true if balancing is on for the specified cell. False if off.
 * To check if any cell is balancing, pass -1 as the cellIndex (default)
 */
bool BCC::isCellBalancing(const int8_t cellIndex)
{
    return isChannelBalancing(getChannelIndex(cellIndex));
}

/**
 * Checks if this BCC is still present in the chain by reading it's CID
 */
bool BCC::isPresent()
{

    uint16_t regVal;
    bcc_status_t status;
    status = regRead(MC33771C_INIT_OFFSET, 1U, &regVal);

    if (status != BCC_STATUS_SUCCESS)
    {
        return false;
    }

    // This should be impossible, but let's check anyways
    if ((regVal & MC33771C_INIT_CID_MASK) != mCID)
    {

        PRINTF_ERR("[BCC] Retrieved CID (%d) does not correspond to CID it should be (%d)\n", regVal & MC33771C_INIT_CID_MASK, mCID);
        return false;
    }

    return true;
}
#include "bcc/bcc.h"

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

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*! @brief RESET de-glitch filter (t_RESETFLT, typ.) in [us]. */
#define BCC_T_RESETFLT_US 100U

/*! @brief CSB wake-up de-glitch filter, low to high transition
 * (CSB_WU_FLT, max.) in [us].
 * MC33771C: Max 80 us
 * MC33772C: Max 65 us */
#define BCC_CSB_WU_FLT_US 80U

/*! @brief Power up duration (t_WAKE-UP, max.) in [us]. */
#define BCC_T_WAKE_UP_US 440U

/*! @brief SOC to data ready (includes post processing of data, ADC_CFG[AVG]=0)
 * (in [us]), typical value. */
#define BCC_T_EOC_TYP_US 520U

/*! @brief Timeout for SOC to data ready (ADC_CFG[AVG]=0) (in [us]).
 * Note: The typical value is 520 us, the maximal one 546 us. */
#define BCC_T_EOC_TIMEOUT_US 650U

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
},{
    0x0000, /* Unsupported number of cells. */
    0x0000, /* Unsupported number of cells. */
    0x0000, /* Unsupported number of cells. */
    0x0023, /* 3 cells. */
    0x0033, /* 4 cells. */
    0x003B, /* 5 cells. */
    0x003F,  /* 6 cells. */
    0x0000, /* Unsupported number of cells. */
    0x0000, /* Unsupported number of cells. */
    0x0000, /* Unsupported number of cells. */
    0x0000, /* Unsupported number of cells. */
    0x0000, /* Unsupported number of cells. */
    0x0000, /* Unsupported number of cells. */
    0x0000, /* Unsupported number of cells. */
    0x0000, /* Unsupported number of cells. */
}};

/*FUNCTION**********************************************************************
 *
 * Function Name : assignCid
 * Description   : This function assigns CID to a BCC device that has CID equal
 *                 to zero.
 *
 *END**************************************************************************/
bcc_status_t BCC::assignCid(bool loopBack, uint8_t devicesCnt)
{
    uint16_t writeVal, readVal;
    bcc_status_t status;

    /* Check if unassigned node replies. This is the first reading after device
     * reset. */
    /* Note: In SPI communication mode, the device responds with all bit filed
     * set to zero except message counter and the correct CRC to the very first
     * MCU <-> MC33771C/772C message. */
    status = BCC_Communication::regRead(mMsgCnt, BCC_CID_UNASSIG, MC33771C_INIT_OFFSET, 1U, &readVal);
    if ((status != BCC_STATUS_SUCCESS) && (status != BCC_STATUS_COM_NULL))
    {
        return status;
            
    }

    /* Assign CID;
     * Terminate RDTX_OUT of the last node in TPL setup without loop-back.
     * Stop forwarding only for MC33772C in TPL setup with one node and no
     * loop-back. RDTX_OUT should not be terminated in this case. */
    writeVal = MC33771C_INIT_CID(mCID) |
               MC33771C_INIT_RDTX_IN(MC33771C_INIT_RDTX_IN_DISABLED_ENUM_VAL);

    if ((devicesCnt == 1U) &&
        (!loopBack) &&
        (mDevice == BCC_DEVICE_MC33772C))
    {
        writeVal |= MC33772C_INIT_TPL2_TX_TERM(MC33772C_INIT_TPL2_TX_TERM_DISABLED_ENUM_VAL) |
                    MC33772C_INIT_BUS_FW(MC33772C_INIT_BUS_FW_DISABLED_ENUM_VAL);
    }
    else if (((uint8_t)mCID == devicesCnt) &&
             (!loopBack))
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

    status = BCC_Communication::regWrite(BCC_CID_UNASSIG, MC33771C_INIT_OFFSET, writeVal);
    if (status == BCC_STATUS_SUCCESS)
    {


        /* Check if assigned node replies. */
        status = BCC_Communication::regRead(mMsgCnt, mCID, MC33771C_INIT_OFFSET, 1U, &readVal);

        /* Check the written data. */
        if ((status == BCC_STATUS_SUCCESS) && (writeVal != readVal))
        {
            status = BCC_STATUS_SPI_FAIL;
        }
    }

    if (status != BCC_STATUS_SUCCESS)
    {
        /* Wait and try to assign CID once again. */
        BCC_MCU_WaitUs(750U);

        status = BCC_Communication::regWrite(BCC_CID_UNASSIG, MC33771C_INIT_OFFSET, writeVal);
        if (status == BCC_STATUS_SUCCESS)
        {


            status = BCC_Communication::regRead(mMsgCnt, mCID, MC33771C_INIT_OFFSET, 1U, &readVal);

            /* Check the written data. */
            if ((status == BCC_STATUS_SUCCESS) && (writeVal != readVal))
            {
                status = BCC_STATUS_SPI_FAIL;
            }
        }
    }

    return status;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : setGpioCfg
 * Description   : This function configures selected GPIO/AN pin as analog
 *                 input, digital input or digital output by writing the
 *                 GPIO_CFG1[GPIOx_CFG] bit field.
 *
 *END**************************************************************************/
bcc_status_t BCC::setGpioCfg(const uint8_t gpioSel, const bcc_pin_mode_t mode)
{
    if ((gpioSel >= BCC_GPIO_INPUT_CNT) || (mode > BCC_PIN_DIGITAL_OUT))
    {
        return BCC_STATUS_PARAM_RANGE;
    }

    /* Update the content of GPIO_CFG1 register. */
    return BCC_Communication::regUpdate(mMsgCnt, mCID,
                          MC33771C_GPIO_CFG1_OFFSET,
                          (uint16_t)(MC33771C_GPIO_CFG1_GPIO0_CFG_MASK << (gpioSel * 2U)),
                          (uint16_t)(((uint16_t)mode) << (gpioSel * 2U)));
}

/*******************************************************************************
 * PUBLIC FUNCTIONS
 ******************************************************************************/

/*FUNCTION**********************************************************************
 *
 * Function Name : BCC (constructor)
 * Description   : Sets correct parameters
 *
 *END**************************************************************************/
BCC::BCC(bcc_device_t device, uint8_t cellCount, uint8_t ntcCount, bool currentSenseEnable, bcc_cid_t cid) : mDevice(device), mCellCount(cellCount), mNTCCount(ntcCount), mCurrentSenseEnabled(currentSenseEnable), mCID(cid), mCellMap(s_cellMap[device][cellCount]){}


/*FUNCTION**********************************************************************
 *
 * Function Name : softwareReset
 * Description   : This function resets BCC device using software reset. It
 *                 enters reset via SPI or TPL interface.
 *
 *END**************************************************************************/
bcc_status_t BCC::softwareReset()
{
    return BCC_Communication::regWrite(this->mCID, MC33771C_SYS_CFG1_OFFSET, MC33771C_SYS_CFG1_SOFT_RST(MC33771C_SYS_CFG1_SOFT_RST_ACTIVE_ENUM_VAL));
}

/*FUNCTION**********************************************************************
 *
 * Function Name : meas_StartConversion
 * Description   : This function starts ADC conversion in selected BCC device.
 *                 It sets number of samples to be averaged and Start of
 *                 Conversion bit in ADC_CFG register.
 *
 *END**************************************************************************/
bcc_status_t BCC::meas_StartConversion(const bcc_avg_t avg)
{
    if (avg > BCC_AVG_256)
    {
        return BCC_STATUS_PARAM_RANGE;
    }

    return BCC_Communication::regUpdate(mMsgCnt, mCID, MC33771C_ADC_CFG_OFFSET,
                          MC33771C_ADC_CFG_SOC_MASK | MC33771C_ADC_CFG_AVG_MASK,
                          MC33771C_ADC_CFG_SOC(MC33771C_ADC_CFG_SOC_ENABLED_ENUM_VAL) |
                              MC33771C_ADC_CFG_AVG(avg));
}



/*FUNCTION**********************************************************************
 *
 * Function Name : meas_IsConverting
 * Description   : This function checks status of conversion defined by End of
 *                 Conversion bit in ADC_CFG register.
 *
 *END**************************************************************************/
bcc_status_t BCC::meas_IsConverting(bool *const completed)
{
    uint16_t adcCfgVal;
    bcc_status_t status;

    BCC_MCU_Assert(completed != NULL);

    status = BCC_Communication::regRead(mMsgCnt, mCID, MC33771C_ADC_CFG_OFFSET, 1U, &adcCfgVal);

    *(completed) = ((adcCfgVal & MC33771C_ADC_CFG_EOC_N_MASK) ==
                    MC33771C_ADC_CFG_EOC_N(MC33771C_ADC_CFG_EOC_N_COMPLETED_ENUM_VAL));

    return status;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : meas_StartAndWait
 * Description   : This function starts an on-demand conversion in selected BCC
 *                 device and waits for completion.
 *
 *END**************************************************************************/
bcc_status_t BCC::meas_StartAndWait(const bcc_avg_t avg)
{
    bool complete; /* Conversion complete flag. */
    bcc_status_t status;

    if (avg > BCC_AVG_256)
    {
        return BCC_STATUS_PARAM_RANGE;
    }

    status = meas_StartConversion(avg);
    if (status != BCC_STATUS_SUCCESS)
    {
        return status;
    }

    /* Wait for at least 520 us (16-bit conversion) before polling bit EOC_N
     * to avoid any traffic on the communication bus during conversion. */
    BCC_MCU_WaitUs(((uint32_t)BCC_T_EOC_TYP_US) << ((uint8_t)avg));

    uint32_t wait_time_us = (((uint32_t)BCC_T_EOC_TIMEOUT_US) << ((uint8_t)avg)) - (((uint32_t)BCC_T_EOC_TYP_US) << ((uint8_t)avg));
    uint32_t start_time_us = BCC_MCU_GetUs();

    // status = BCC_MCU_StartTimeout(
    //         (((uint32_t)BCC_T_EOC_TIMEOUT_US) << ((uint8_t)avg)) -
    //         (((uint32_t)BCC_T_EOC_TYP_US) << ((uint8_t)avg)));
    // if (status != BCC_STATUS_SUCCESS)
    // {
    //     return status;
    // }

    do {
        status = meas_IsConverting(&complete);
        if (status != BCC_STATUS_SUCCESS)
        {
            return status;
        }
    } while ((!complete) && ((BCC_MCU_GetUs() - start_time_us < wait_time_us)));

    /* Check once more after timeout expiration because the read command takes
     * several tens/hundreds of microseconds (depends on user code efficiency)
     * and the last read command could be done relatively long before the
     * timeout expiration. */
    if (!complete)
    {
        status = meas_IsConverting(&complete);
        if (status != BCC_STATUS_SUCCESS)
        {
            return status;
        }
    }

    return (complete) ? BCC_STATUS_SUCCESS : BCC_STATUS_COM_TIMEOUT;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : meas_GetRawValues
 * Description   : This function reads the measurement registers and returns raw
 *                 values.
 *
 *END**************************************************************************/
bcc_status_t BCC::meas_GetRawValues(uint16_t *const measurements)
{
    bcc_status_t status;
    uint8_t i;

    BCC_MCU_Assert(measurements != NULL);

    /* Read all the measurement registers.
     * Note: the order and number of registers conforms to the order of measured
     * values in Measurements array, see enumeration bcc_measurements_t. */
    if (mDevice == BCC_DEVICE_MC33771C)
    {
        status = BCC_Communication::regRead(mMsgCnt, mCID, MC33771C_CC_NB_SAMPLES_OFFSET,
                              BCC_MEAS_CNT, measurements);
    }
    else
    {
        status = BCC_Communication::regRead(mMsgCnt, mCID, MC33772C_CC_NB_SAMPLES_OFFSET,
                              (MC33772C_MEAS_STACK_OFFSET - MC33772C_CC_NB_SAMPLES_OFFSET) + 1, measurements);
        if (status != BCC_STATUS_SUCCESS)
        {
            return status;
        }

        /* Skip the reserved registers to speed-up this function. */
        measurements[BCC_MSR_CELL_VOLT14] = 0x0000;
        measurements[BCC_MSR_CELL_VOLT13] = 0x0000;
        measurements[BCC_MSR_CELL_VOLT12] = 0x0000;
        measurements[BCC_MSR_CELL_VOLT11] = 0x0000;
        measurements[BCC_MSR_CELL_VOLT10] = 0x0000;
        measurements[BCC_MSR_CELL_VOLT9] = 0x0000;
        measurements[BCC_MSR_CELL_VOLT8] = 0x0000;
        measurements[BCC_MSR_CELL_VOLT7] = 0x0000;

        status = BCC_Communication::regRead(mMsgCnt, mCID, MC33772C_MEAS_CELL6_OFFSET,
                              (MC33772C_MEAS_VBG_DIAG_ADC1B_OFFSET - MC33772C_MEAS_CELL6_OFFSET) + 1,
                              (uint16_t *)(measurements + ((uint8_t)BCC_MSR_CELL_VOLT6)));
    }

    /* Mask the read registers.
     * Note: Nothing to mask in CC_NB_SAMPLES, COULOMB_CNT1 and COULOMB_CNT2
     * registers. */
    measurements[BCC_MSR_ISENSE1] &= MC33771C_MEAS_ISENSE1_MEAS_I_MSB_MASK;
    measurements[BCC_MSR_ISENSE2] &= MC33771C_MEAS_ISENSE2_MEAS_I_LSB_MASK;

    for (i = (uint8_t)BCC_MSR_STACK_VOLT; i < BCC_MEAS_CNT; i++)
    {
        measurements[i] &= MC33771C_MEAS_STACK_MEAS_STACK_MASK;
    }

    return status;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : meas_GetCoulombCounter
 * Description   : This function reads the Coulomb counter registers.
 *
 *END**************************************************************************/
bcc_status_t BCC::meas_GetCoulombCounter(bcc_cc_data_t *const cc)
{
    bcc_status_t status;
    uint16_t readVal[3];

    BCC_MCU_Assert(cc != NULL);

    status = BCC_Communication::regRead(mMsgCnt, mCID, MC33771C_CC_NB_SAMPLES_OFFSET, 3U, readVal);
    if (status != BCC_STATUS_SUCCESS)
    {
        return status;
    }

    cc->nbSamples = readVal[0];
    cc->ccAccumulator = BCC_GET_COULOMB_CNT(readVal[1], readVal[2]);

    return BCC_STATUS_SUCCESS;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : meas_GetIsenseVoltage
 * Description   : This function reads the ISENSE measurement and converts it to
 *                 [uV].
 *
 *END**************************************************************************/
bcc_status_t BCC::meas_GetIsenseVoltage(int32_t *const isenseVolt)
{
    bcc_status_t status;
    uint16_t readVal[2];

    BCC_MCU_Assert(isenseVolt != NULL);

    status = BCC_Communication::regRead(mMsgCnt, mCID, MC33771C_MEAS_ISENSE1_OFFSET, 2U, readVal);
    if (status != BCC_STATUS_SUCCESS)
    {
        return status;
    }

    if ((readVal[0] & readVal[1] & MC33771C_MEAS_ISENSE1_DATA_RDY_MASK) == 0U)
    {
        return BCC_STATUS_DATA_RDY;
    }

    *isenseVolt = BCC_GET_ISENSE_VOLT(readVal[0], readVal[1]);

    return BCC_STATUS_SUCCESS;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : meas_GetStackVoltage
 * Description   : This function reads the stack measurement and converts it to
 *                 [uV].
 *
 *END**************************************************************************/
bcc_status_t BCC::meas_GetStackVoltage(uint32_t *const stackVolt)
{
    bcc_status_t status;
    uint16_t readVal;

    BCC_MCU_Assert(stackVolt != NULL);

    status = BCC_Communication::regRead(mMsgCnt, mCID, MC33771C_MEAS_STACK_OFFSET, 1U, &readVal);
    if (status != BCC_STATUS_SUCCESS)
    {
        return status;
    }

    if ((readVal & MC33771C_MEAS_STACK_DATA_RDY_MASK) == 0U)
    {
        return BCC_STATUS_DATA_RDY;
    }

    *stackVolt = BCC_GET_STACK_VOLT(readVal);

    return BCC_STATUS_SUCCESS;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : meas_GetCellVoltages
 * Description   : This function reads the cell measurements and converts them
 *                 to [uV].
 *
 *END**************************************************************************/
bcc_status_t BCC::meas_GetCellVoltages(uint32_t *const cellVolt)
{
    bcc_status_t status;
    uint16_t readVal[BCC_MAX_CELLS];
    uint8_t i, cellCnt;

    BCC_MCU_Assert(cellVolt != NULL);

    cellCnt = BCC_MAX_CELLS_DEV(mDevice);

    /* Read the measurement registers. */
    status = BCC_Communication::regRead(mMsgCnt, mCID,
                          (mDevice == BCC_DEVICE_MC33771C) ? MC33771C_MEAS_CELL14_OFFSET : MC33771C_MEAS_CELL6_OFFSET,
                          cellCnt, readVal);
    if (status != BCC_STATUS_SUCCESS)
    {
        return status;
    }

    /* Convert measurements to [uV], change the cell order and check the data-ready flag. */
    for (i = 0; i < cellCnt; i++)
    {
        cellVolt[cellCnt - (i + 1)] = BCC_GET_VOLT(readVal[i]);
        readVal[0] &= readVal[i];
    }

    if ((readVal[0] & MC33771C_MEAS_CELL1_DATA_RDY_MASK) == 0U)
    {
        return BCC_STATUS_DATA_RDY;
    }

    return BCC_STATUS_SUCCESS;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : meas_GetCellVoltage
 * Description   : This function reads the voltage measurement of a selected
 *                 cell and converts it to [uV].
 *
 *END**************************************************************************/
bcc_status_t BCC::meas_GetCellVoltage(uint8_t cellIndex, uint32_t *const cellVolt)
{
    bcc_status_t status;
    uint16_t readVal;

    BCC_MCU_Assert(cellVolt != NULL);

    if (cellIndex >= BCC_MAX_CELLS_DEV(mDevice))
    {
        return BCC_STATUS_PARAM_RANGE;
    }

    status = BCC_Communication::regRead(mMsgCnt, mCID, MC33771C_MEAS_CELL1_OFFSET - cellIndex, 1U, &readVal);
    if (status != BCC_STATUS_SUCCESS)
    {
        return status;
    }

    if ((readVal & MC33771C_MEAS_CELL1_DATA_RDY_MASK) == 0U)
    {
        return BCC_STATUS_DATA_RDY;
    }

    *cellVolt = BCC_GET_VOLT(readVal);

    return BCC_STATUS_SUCCESS;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : meas_GetAnVoltages
 * Description   : This function reads the voltage measurement for all ANx
 *                 and converts them to [uV]. Intended for ANx configured for
 *                 absolute measurements only!
 *
 *END**************************************************************************/
bcc_status_t BCC::meas_GetAnVoltages(uint32_t *const anVolt)
{
    bcc_status_t status;
    uint16_t readVal[BCC_GPIO_INPUT_CNT];
    uint8_t i;

    BCC_MCU_Assert(anVolt != NULL);

    /* Read the measurement registers. */
    status = BCC_Communication::regRead(mMsgCnt, mCID, MC33771C_MEAS_AN6_OFFSET,
                          BCC_GPIO_INPUT_CNT, readVal);
    if (status != BCC_STATUS_SUCCESS)
    {
        return status;
    }

    /* Convert measurements to [uV] and check the data-ready flag. */
    for (i = 0; i < BCC_GPIO_INPUT_CNT; i++)
    {
        anVolt[i] = BCC_GET_VOLT(readVal[i]);
        readVal[0] &= readVal[i];
    }

    if ((readVal[0] & MC33771C_MEAS_AN0_DATA_RDY_MASK) == 0U)
    {
        return BCC_STATUS_DATA_RDY;
    }

    return BCC_STATUS_SUCCESS;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : meas_GetAnVoltage
 * Description   : This function reads the voltage measurement of a selected
 *                 ANx and converts it to [uV]. Intended for ANx configured for
 *                 absolute measurements only!
 *
 *END**************************************************************************/
bcc_status_t BCC::meas_GetAnVoltage(uint8_t anIndex, uint32_t *const anVolt)
{
    bcc_status_t status;
    uint16_t readVal;

    BCC_MCU_Assert(anVolt != NULL);

    if (anIndex >= BCC_GPIO_INPUT_CNT)
    {
        return BCC_STATUS_PARAM_RANGE;
    }

    status = BCC_Communication::regRead(mMsgCnt, mCID, MC33771C_MEAS_AN0_OFFSET - anIndex, 1U, &readVal);
    if (status != BCC_STATUS_SUCCESS)
    {
        return status;
    }

    if ((readVal & MC33771C_MEAS_AN0_DATA_RDY_MASK) == 0U)
    {
        return BCC_STATUS_DATA_RDY;
    }

    *anVolt = BCC_GET_VOLT(readVal);

    return BCC_STATUS_SUCCESS;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : meas_GetIcTemp
 * Description   : This function reads the BCC temperature and converts it to
 *                 the selected unit.
 *
 *END**************************************************************************/
bcc_status_t BCC::meas_GetIcTemperature(bcc_temp_unit_t unit, int16_t *const icTemp)
{
    bcc_status_t status;
    uint16_t readVal;

    BCC_MCU_Assert(icTemp != NULL);

    if (unit > BCC_TEMP_FAHRENHEIT)
    {
        return BCC_STATUS_PARAM_RANGE;
    }

    status = BCC_Communication::regRead(mMsgCnt, mCID, MC33771C_MEAS_IC_TEMP_OFFSET, 1U, &readVal);
    if (status != BCC_STATUS_SUCCESS)
    {
        return status;
    }

    if ((readVal & MC33771C_MEAS_IC_TEMP_DATA_RDY_MASK) == 0U)
    {
        return BCC_STATUS_DATA_RDY;
    }

    if (unit == BCC_TEMP_CELSIUS)
    {
        *icTemp = BCC_GET_IC_TEMP_C(readVal);
    }
    else if (unit == BCC_TEMP_FAHRENHEIT)
    {
        *icTemp = BCC_GET_IC_TEMP_F(readVal);
    }
    else
    {
        *icTemp = BCC_GET_IC_TEMP_K(readVal);
    }

    return BCC_STATUS_SUCCESS;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : fault_GetStatus
 * Description   : This function reads the fault status registers of the BCC
 *                 device.
 *
 *END**************************************************************************/
bcc_status_t BCC::fault_GetStatus(uint16_t *const fltStatus)
{
    bcc_status_t status;

    BCC_MCU_Assert(fltStatus != NULL);

    /* Read CELL_OV_FLT and CELL_UV_FLT. */
    status = BCC_Communication::regRead(mMsgCnt, mCID, MC33771C_CELL_OV_FLT_OFFSET, 2U, &fltStatus[BCC_FS_CELL_OV]);
    if (status != BCC_STATUS_SUCCESS)
    {
        return status;
    }

    /* Read CB_OPEN_FLT, CB_SHORT_FLT. */
    status = BCC_Communication::regRead(mMsgCnt, mCID, MC33771C_CB_OPEN_FLT_OFFSET, 2U, &fltStatus[BCC_FS_CB_OPEN]);
    if (status != BCC_STATUS_SUCCESS)
    {
        return status;
    }

    /* Read GPIO_STS, AN_OT_UT_FLT, GPIO_SHORT_Anx_OPEN_STS. */
    status = BCC_Communication::regRead(mMsgCnt, mCID, MC33771C_GPIO_STS_OFFSET, 3U, &fltStatus[BCC_FS_GPIO_STATUS]);
    if (status != BCC_STATUS_SUCCESS)
    {
        return status;
    }

    /* Read COM_STATUS, FAULT1_STATUS, FAULT2_STATUS and FAULT3_STATUS. */
    return BCC_Communication::regRead(mMsgCnt, mCID, MC33771C_COM_STATUS_OFFSET, 4U, &fltStatus[BCC_FS_COMM]);
}

/*FUNCTION**********************************************************************
 *
 * Function Name : fault_ClearStatus
 * Description   : This function clears selected fault status register.
 *
 *END**************************************************************************/
bcc_status_t BCC::fault_ClearStatus(const bcc_fault_status_t statSel)
{
    /* This array is intended for conversion of bcc_fault_status_t value to
     * a BCC register address. */
    const uint8_t regAddrMap[BCC_STAT_CNT] = {
        MC33771C_CELL_OV_FLT_OFFSET, MC33771C_CELL_UV_FLT_OFFSET,
        MC33771C_CB_OPEN_FLT_OFFSET, MC33771C_CB_SHORT_FLT_OFFSET,
        MC33771C_GPIO_STS_OFFSET, MC33771C_AN_OT_UT_FLT_OFFSET,
        MC33771C_GPIO_SHORT_ANX_OPEN_STS_OFFSET, MC33771C_COM_STATUS_OFFSET,
        MC33771C_FAULT1_STATUS_OFFSET, MC33771C_FAULT2_STATUS_OFFSET,
        MC33771C_FAULT3_STATUS_OFFSET};

    if ((uint32_t)statSel >= BCC_STAT_CNT)
    {
        return BCC_STATUS_PARAM_RANGE;
    }

    return BCC_Communication::regWrite(mCID, regAddrMap[statSel], 0x0000U);
}

/*FUNCTION**********************************************************************
 *
 * Function Name : GPIO_SetMode
 * Description   : This function sets the mode of one BCC GPIOx/ANx pin.
 *
 *END**************************************************************************/
bcc_status_t BCC::GPIO_SetMode(const uint8_t gpioSel, const bcc_pin_mode_t mode)
{
    bcc_status_t status = BCC_STATUS_PARAM_RANGE;

    if (gpioSel >= BCC_GPIO_INPUT_CNT)
    {
        return BCC_STATUS_PARAM_RANGE;
    }

    if ((mode == BCC_PIN_WAKE_UP_IN) && (gpioSel == 0U))
    {
        /* Set GPIO0 to digital input and enable the wake-up capability. */
        status = setGpioCfg(0U, BCC_PIN_DIGITAL_IN);
        if (status == BCC_STATUS_SUCCESS)
        {
            status = BCC_Communication::regUpdate(mMsgCnt, mCID,
                                    MC33771C_GPIO_CFG2_OFFSET,
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
            status = BCC_Communication::regUpdate(mMsgCnt, mCID,
                                    MC33771C_GPIO_CFG2_OFFSET,
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
            status = BCC_Communication::regUpdate(mMsgCnt, mCID,
                                    MC33771C_GPIO_CFG2_OFFSET,
                                    MC33771C_GPIO_CFG2_GPIO0_WU_MASK,
                                    MC33771C_GPIO_CFG2_GPIO0_WU(MC33771C_GPIO_CFG2_GPIO0_WU_NO_WAKEUP_ENUM_VAL));
        }
        else if (gpioSel == 2U)
        {
            /* Disable the conversion trigger. */
            status = BCC_Communication::regUpdate(mMsgCnt, mCID,
                                    MC33771C_GPIO_CFG2_OFFSET,
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

/*FUNCTION**********************************************************************
 *
 * Function Name : GPIO_ReadPin
 * Description   : This function reads a value of one BCC GPIO pin.
 *
 *END**************************************************************************/
bcc_status_t BCC::GPIO_ReadPin(const uint8_t gpioSel, bool *const val)
{
    bcc_status_t status;
    uint16_t gpioStsVal;

    BCC_MCU_Assert(val != NULL);

    if (gpioSel >= BCC_GPIO_INPUT_CNT)
    {
        return BCC_STATUS_PARAM_RANGE;
    }

    /* Read and update content of GPIO_CFG2 register. */
    status = BCC_Communication::regRead(mMsgCnt, mCID, MC33771C_GPIO_STS_OFFSET, 1U, &gpioStsVal);
    *val = (gpioStsVal & (1U << gpioSel)) > 0U;

    return status;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : GPIO_SetOutput
 * Description   : This function sets output value of one BCC GPIO pin.
 *
 *END**************************************************************************/
bcc_status_t BCC::GPIO_SetOutput(const uint8_t gpioSel, const bool val)
{
    if (gpioSel >= BCC_GPIO_INPUT_CNT)
    {
        return BCC_STATUS_PARAM_RANGE;
    }

    /* Update the content of GPIO_CFG2 register. */
    return BCC_Communication::regUpdate(mMsgCnt, mCID, MC33771C_GPIO_CFG2_OFFSET,
                          (uint16_t)(1U << gpioSel),
                          (uint16_t)((val ? 1U : 0U) << gpioSel));
}

/*FUNCTION**********************************************************************
 *
 * Function Name : CB_Enable
 * Description   : This function enables or disables the cell balancing via
 *                 SYS_CFG1[CB_DRVEN] bit.
 *
 *END**************************************************************************/
bcc_status_t BCC::CB_Enable(const bool enable)
{
    return BCC_Communication::regUpdate(mMsgCnt, mCID, MC33771C_SYS_CFG1_OFFSET, MC33771C_SYS_CFG1_CB_DRVEN_MASK,
                          enable ? MC33771C_SYS_CFG1_CB_DRVEN(MC33771C_SYS_CFG1_CB_DRVEN_ENABLED_ENUM_VAL)
                                 : MC33771C_SYS_CFG1_CB_DRVEN(MC33771C_SYS_CFG1_CB_DRVEN_DISABLED_ENUM_VAL));
}

/*FUNCTION**********************************************************************
 *
 * Function Name : CB_SetIndividual
 * Description   : This function enables or disables cell balancing for a
 *                 specified cell and sets its timer.
 *
 *END**************************************************************************/
bcc_status_t BCC::CB_SetIndividual(const uint8_t cellIndex, const bool enable, const uint16_t timer)
{
    uint16_t cbxCfgVal;

    if (cellIndex >= BCC_MAX_CELLS_DEV(mDevice))
    {
        return BCC_STATUS_PARAM_RANGE;
    }

    if (timer > MC33771C_CB1_CFG_CB_TIMER_MASK)
    {
        return BCC_STATUS_PARAM_RANGE;
    }

    cbxCfgVal = enable ? MC33771C_CB1_CFG_CB_EN(MC33771C_CB1_CFG_CB_EN_ENABLED_ENUM_VAL)
                       : MC33771C_CB1_CFG_CB_EN(MC33771C_CB1_CFG_CB_EN_DISABLED_ENUM_VAL);
    cbxCfgVal |= MC33771C_CB1_CFG_CB_TIMER(timer);

    return BCC_Communication::regWrite(mCID, MC33771C_CB1_CFG_OFFSET + cellIndex, cbxCfgVal);
}

/*FUNCTION**********************************************************************
 *
 * Function Name : CB_Pause
 * Description   : This function pauses cell balancing.
 *
 *END**************************************************************************/
bcc_status_t BCC::CB_Pause(const bool pause)
{
    return BCC_Communication::regUpdate(mMsgCnt, mCID, MC33771C_SYS_CFG1_OFFSET, MC33771C_SYS_CFG1_CB_MANUAL_PAUSE_MASK,
                          (pause) ? MC33771C_SYS_CFG1_CB_MANUAL_PAUSE(MC33771C_SYS_CFG1_CB_MANUAL_PAUSE_ENABLED_ENUM_VAL)
                                  : MC33771C_SYS_CFG1_CB_MANUAL_PAUSE(MC33771C_SYS_CFG1_CB_MANUAL_PAUSE_DISABLED_ENUM_VAL));
}

/*FUNCTION**********************************************************************
 *
 * Function Name : fuseMirror_Read
 * Description   : This function reads a fuse mirror register of selected BCC
 *                 device.
 *
 *END**************************************************************************/
bcc_status_t BCC::fuseMirror_Read(const uint8_t fuseAddr, uint16_t *const value)
{
    bcc_status_t status;

    BCC_MCU_Assert(value != NULL);

    if (fuseAddr > ((mDevice == BCC_DEVICE_MC33771C) ? MC33771C_MAX_FUSE_READ_ADDR : MC33772C_MAX_FUSE_READ_ADDR))
    {
        return BCC_STATUS_PARAM_RANGE;
    }

    status = BCC_Communication::regWrite(mCID, MC33771C_FUSE_MIRROR_CNTL_OFFSET,
                           MC33771C_FUSE_MIRROR_CNTL_FMR_ADDR(fuseAddr) |
                               MC33771C_FUSE_MIRROR_CNTL_FSTM(MC33771C_FUSE_MIRROR_CNTL_FSTM_LOCKED_ENUM_VAL) |
                               MC33771C_FUSE_MIRROR_CNTL_FST(MC33771C_FUSE_MIRROR_CNTL_FST_SPI_WRITE_ENABLE_ENUM_VAL));
    if (status != BCC_STATUS_SUCCESS)
    {
        return status;
    }

    return BCC_Communication::regRead(mMsgCnt, mCID, MC33771C_FUSE_MIRROR_DATA_OFFSET, 1U, value);
}

/*FUNCTION**********************************************************************
 *
 * Function Name : fuseMirror_Write
 * Description   : This function writes a fuse mirror register of a BCC device
 *                 specified by CID.
 *
 *END**************************************************************************/
bcc_status_t BCC::fuseMirror_Write(const uint8_t fuseAddr, const uint16_t value)
{
    bcc_status_t status;

    if (fuseAddr > ((mDevice == BCC_DEVICE_MC33771C) ? MC33771C_MAX_FUSE_WRITE_ADDR : MC33772C_MAX_FUSE_WRITE_ADDR))
    {
        return BCC_STATUS_PARAM_RANGE;
    }

    /* FUSE_MIRROR_CNTL to enable writing. */
    status = BCC_Communication::regWrite(mCID, MC33771C_FUSE_MIRROR_CNTL_OFFSET,
                           MC33771C_FUSE_MIRROR_CNTL_FMR_ADDR(0U) |
                               MC33771C_FUSE_MIRROR_CNTL_FSTM(MC33771C_FUSE_MIRROR_CNTL_FSTM_UNLOCKED_ENUM_VAL) |
                               MC33771C_FUSE_MIRROR_CNTL_FST(MC33771C_FUSE_MIRROR_CNTL_FST_SPI_WRITE_ENABLE_ENUM_VAL));
    if (status != BCC_STATUS_SUCCESS)
    {
        return status;
    }

    /* Send the fuse address. */
    status = BCC_Communication::regWrite(mCID, MC33771C_FUSE_MIRROR_CNTL_OFFSET,
                           MC33771C_FUSE_MIRROR_CNTL_FMR_ADDR(fuseAddr) |
                               MC33771C_FUSE_MIRROR_CNTL_FSTM(MC33771C_FUSE_MIRROR_CNTL_FSTM_UNLOCKED_ENUM_VAL) |
                               MC33771C_FUSE_MIRROR_CNTL_FST(MC33771C_FUSE_MIRROR_CNTL_FST_SPI_WRITE_ENABLE_ENUM_VAL));
    if (status != BCC_STATUS_SUCCESS)
    {
        return status;
    }

    status = BCC_Communication::regWrite(mCID, MC33771C_FUSE_MIRROR_DATA_OFFSET, value);
    if (status != BCC_STATUS_SUCCESS)
    {
        return status;
    }

    /* FUSE_MIRROR_CNTL to low power. */
    return BCC_Communication::regWrite(mCID, MC33771C_FUSE_MIRROR_CNTL_OFFSET,
                         MC33771C_FUSE_MIRROR_CNTL_FMR_ADDR(0U) |
                             MC33771C_FUSE_MIRROR_CNTL_FSTM(MC33771C_FUSE_MIRROR_CNTL_FSTM_UNLOCKED_ENUM_VAL) |
                             MC33771C_FUSE_MIRROR_CNTL_FST(MC33771C_FUSE_MIRROR_CNTL_FST_LP_ENUM_VAL));
}

/*FUNCTION**********************************************************************
 *
 * Function Name : GUID_Read
 * Description   : This function reads an unique serial number of the BCC device
 *                 from the content of mirror registers.
 *
 *END**************************************************************************/
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


/*FUNCTION**********************************************************************
 *
 * Function Name : hasValidConfig
 * Description   : This function checks if the BCC has a valid configuration.
 *
 *END**************************************************************************/
bool BCC::hasValidConfig() {
    if (mDevice == NULL || mCellCount == 0) {
        return false;
    }

    if (mNTCCount > 7) {
        return false;
    }

    if (mDevice == BCC_DEVICE_MC33771C)
        {
            if (!BCC_IS_IN_RANGE(mCellCount, MC33771C_MIN_CELLS, MC33771C_MAX_CELLS))
            {
                return false;
            }
  
        }
        else if (mDevice == BCC_DEVICE_MC33772C)
        {
            if (!BCC_IS_IN_RANGE(mCellCount, MC33772C_MIN_CELLS, MC33772C_MAX_CELLS))
            {
                return false;
            }

        }
        else
        {
            return false;
        }

    return true;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : currentSenseEnabled
 * Description   : This function returns if current sensing is enabled.
 *
 *END**************************************************************************/
bool BCC::currentSenseEnabled() {
    return mCurrentSenseEnabled;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : getCellCount
 * Description   : This function returns the number of cells.
 *
 *END**************************************************************************/
uint8_t BCC::getCellCount() {
    return mCellCount;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : getNTCCount
 * Description   : This function returns the number of NTCs.
 *
 *END**************************************************************************/
uint8_t BCC::getNTCCount() {
    return mNTCCount;
}
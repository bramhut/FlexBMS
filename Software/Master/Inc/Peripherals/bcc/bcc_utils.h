/*
 * Copyright 2020 NXP
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * o Redistributions of source code must retain the above copyright notice, this list
 *   of conditions and the following disclaimer.
 *
 * o Redistributions in binary form must reproduce the above copyright notice, this
 *   list of conditions and the following disclaimer in the documentation and/or
 *   other materials provided with the distribution.
 *
 * o Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*!
 * @file bcc_utils.h
 *
 * Macros for conversion of measurement results, voltage thresholds and other
 * configurations for MC33771C and MC33772C.
 */

#pragma once

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "MC33771C.h"
#include "MC33772C.h"
#include "TimeFunctions.h"
#include "HelperFunc.h"
#include "cmsis_os.h"
#include <math.h>

/*******************************************************************************
 * Macros for Time related stuff
 ******************************************************************************/

#define BCC_MCU_WaitMs(delay_ms) delay(delay_ms)
#define BCC_MCU_WaitUs(delay_us) delayUS(delay_us)
#define BCC_MCU_GetUs() micros()
#define BCC_MCU_Assert(x) configASSERT(x)

/*******************************************************************************
 * Macros for conversion of the measurement results
 ******************************************************************************/

/*!
 * @brief Returns a 32 bit signed value of Coulomb counter composed from values
 * of registers COULOMB_CNT1 (higher part) and COULOMB_CNT2 (lower part).
 *
 * @param coulombCnt1 Content of register COULOMB_CNT1.
 * @param coulombCnt2 Content of register COULOMB_CNT2.
 */
#define BCC_GET_COULOMB_CNT(coulombCnt1, coulombCnt2)                                                     \
    ((int32_t)(((uint32_t)((coulombCnt1) & MC33771C_TH_COULOMB_CNT_MSB_TH_COULOMB_CNT_MSB_MASK) << 16U) | \
               ((uint32_t)(coulombCnt2) & MC33771C_TH_COULOMB_CNT_LSB_TH_COULOMB_CNT_LSB_MASK)))

/*!
 * @brief Returns a raw value of ISENSE measurement composed from values
 * of registers MEAS_ISENSE1 and MEAS_ISENSE2.
 *
 * @param measISense1 Content of register MEAS_ISENSE1.
 * @param measISense2 Content of register MEAS_ISENSE2.
 */
#define BCC_GET_ISENSE_RAW(measISense1, measISense2)                             \
    ((((uint32_t)(measISense1) & MC33771C_MEAS_ISENSE1_MEAS_I_MSB_MASK) << 4U) | \
     ((uint32_t)(measISense2) & MC33771C_MEAS_ISENSE2_MEAS_I_LSB_MASK))

/*!
 * @brief Performed a sign extension on the raw value of ISENSE measurement
 * (result of macro BCC_GET_ISENSE_RAW). Returns a signed 32 bit value.
 *
 * @param iSenseRaw Raw value of measured current (result of
 *                  BCC_GET_ISENSE_RAW macro).
 */
#define BCC_GET_ISENSE_RAW_SIGN(iSenseRaw) \
    ((int32_t)(((iSenseRaw) & 0x040000U) ? ((iSenseRaw) | 0xFFF80000U) : (iSenseRaw)))

/*!
 * @brief This macro calculates ISENSE value (in [uV]) from the content of
 * MEAS_ISENSE1 and MEAS_ISENSE2 registers.
 *
 * Resolution of the measurement is 0.6 uV/LSB (V2RES).
 *
 * Note: V_IND (ISENSE+/ISENSE- Differential Input Voltage Range) is between
 * -150 mV and 150 mV (see datasheet).
 *
 * @param iSense1 Content of register MEAS_ISENSE1.
 * @param iSense2 Content of register MEAS_ISENSE2.
 *
 * @return ISENSE voltage in [uV]; int32_t type.
 */
#define BCC_GET_ISENSE_VOLT(iSense1, iSense2) \
    ((BCC_GET_ISENSE_RAW_SIGN(BCC_GET_ISENSE_RAW(iSense1, iSense2)) * 6) / 10)

/*!
 * @brief This macro calculates the measured current (in [mA]) from the SHUNT
 * resistance and from the content of MEAS_ISENSE1 and MEAS_ISENSE2 registers.
 *
 * Resolution of the measurement is (600/R_SHUNT) mA/LSB when R_SHUNT in [uOhm].
 *
 * @param rShunt  Resistance of Shunt resistor (in [uOhm]).
 * @param iSense1 Content of register MEAS_ISENSE1.
 * @param iSense2 Content of register MEAS_ISENSE2.
 *
 * @return ISENSE current in [mA]; int32_t type.
 */
#define BCC_GET_ISENSE_AMP(rShunt, iSense1, iSense2) ( \
    (BCC_GET_ISENSE_RAW_SIGN(BCC_GET_ISENSE_RAW(iSense1, iSense2)) * 600) / (int32_t)(rShunt))

/*!
 * @brief Masks a register value and returns a raw measured value.
 *
 * This macro can be used only for MEAS_STACK, MEAS_CELLx, MEAS_ANx,
 * MEAS_VBG_DIAG_ADC1x and MEAS_IC_TEMP registers!
 *
 * @param reg Value from a measurement register (MEAS_STACK, MEAS_CELLx,
 *            MEAS_ANx, MEAS_IC_TEMP or MEAS_VBG_DIAG_ADC1x).
 */
#define BCC_GET_MEAS_RAW(reg) \
    ((reg) & MC33771C_MEAS_STACK_MEAS_STACK_MASK)

/*!
 * @brief Converts a value of the MEAS_STACK register to [uV].
 *
 * Resolution of the register is 2.4414 mV/LSB.
 * Result is in the range of 0 and 80 000 000 uV.
 *
 * @param reg Value of the MEAS_STACK register.
 *
 * @return Converted value in [uV]; uint32_t type.
 */
#define BCC_GET_STACK_VOLT(reg) \
    ((uint32_t)BCC_GET_MEAS_RAW(reg) * 24414U / 10U)

/*!
 * @brief Converts a value of MEAS_CELLx, MEAS_ANx (absolute measurement),
 * MEAS_VBG_DIAG_ADC1x registers to [uV].
 *
 * Resolution of these registers is 152.58789 uV/LSB.
 * Result is in the range of 0 and 4 999 847 uV.
 *
 * Note: 78125/512 = 152.5878906 provides the best accuracy in 32b arithmetic
 * w/o any overflow. A logical shift speeds up the operation.
 *
 * @param reg Value of a measurement register.
 *
 * @return Converted value in [uV]; uint32_t type.
 */
#define BCC_GET_VOLT(reg) \
    (((uint32_t)BCC_GET_MEAS_RAW(reg) * 78125U) >> 9)

/*!
 * @brief Converts a value of MEAS_ANx (ratiometric measurement) MC33771C
 * registers to [uV].
 *
 * Resolution of these registers is: VCOM*30.51851 uV/LSB.
 * Result is in the range of 0 and (1000000 * VCOM) uV.
 *
 * Note: Instead of operation ((reg * 30.51851) * VCOM) / 1000, which could
 * overflow, ((reg * 15.62547712) * VCOM) / 512 is used.
 *
 * @param reg  Value of a measurement register.
 * @param vCom VCOM voltage in [mV], max 5800 mV.
 *
 * @return Converted value in [uV]; uint32_t type.
 */
#define MC33771C_GET_AN_RATIO_VOLT(reg, vCom) \
    (((((uint32_t)BCC_GET_MEAS_RAW(reg) * 3922U) / 251U) * (vCom)) >> 9)

/*!
 * @brief Converts a value of MEAS_ANx (ratiometric measurement) MC33771C
 * registers to [uV].
 *
 * Resolution of these registers is: VCOM*30.5176 uV/LSB.
 * Result is in the range of 0 and (999970 * VCOM) uV.
 *
 * Note: Instead of operation ((reg * 30.5176) * VCOM) / 1000, which could
 * overflow, ((reg * 15.6250112) * VCOM) / 512 is used.
 *
 * @param reg  Value of a measurement register.
 * @param vCom VCOM voltage in [mV], max 5800 mV.
 *
 * @return Converted value in [uV]; uint32_t type.
 */
#define MC33772C_GET_AN_RATIO_VOLT(reg, vCom) \
    (((((uint32_t)BCC_GET_MEAS_RAW(reg) * 39422U) / 2523U) * (vCom)) >> 9)

/*!
 * @brief Converts a value of the MEAS_IC_TEMP register to a temperature.
 * Max range [-20, 100] C.
 *
 * @param reg Value of the MEAS_IC_TEMP register.
 *
 * @return Converted value in [K] multiplied by 10
 *         (i.e. resolution of 0.1 K); int32_t type.
 */
#define BCC_GET_IC_TEMP(reg) \
    ((uint16_t) (((((uint32_t)BCC_GET_MEAS_RAW(reg)) * 16 - 126'575U) << 11) / 1875))


/*!
 * @brief Converts a voltage to a NTC temperature. Max range [-20, 100] C.
 *
 * @param voltage Value of the MEAS_IC_TEMP register (uint16_t).
 * @param NTCResistance NTC resistance in [Ohm] (double).
 * @param NTCBeta NTC Beta value [K] (double)
 *
 * @return Converted value in [0 = -20C, 0xFFFF = 100C] (uint16_t)
 */
#define BCC_VOLT_TO_TEMPRAW(voltage, NTCResistance, NTCBeta) \
    ((uint16_t)(-138253.65333333332 + (8192*NTCBeta)/ \
    (15.*log((-6800*exp(0.0033540164346805303*NTCBeta)* \
          voltage)/(NTCResistance*((int16_t)voltage - 32768))))))
/*!
 * @brief Converts a NTC temperature [C] into a voltage (raw register value for AN threshold)
 *
 * @param temperature Temperature in [C] (double)
 * @param NTCResistance NTC resistance in [Ohm] (double).
 * @param NTCBeta NTC Beta value [K] (double)
 *
 * @return Converted value (uint16_t)
 */
#define BCC_TEMP_TO_VOLT(temp, NTCResistance, NTCBeta) \
    ((uint16_t)((1024.*exp(NTCBeta/(273.15 + temp))*NTCResistance)/ \
   (6800.*exp(0.00335401643468053*NTCBeta) + \
     exp(NTCBeta/(273.15 + temp))*NTCResistance)))


/*!
 * @brief Converts a NTC temperature [raw] into a readable format [C]
 *
 * @param temperature Temperature in [raw] (uint16_t)
 *
 * @return Converted value [C] (double)
 */
#define BCC_TEMPRAW_TO_TEMP(tempraw) \
    (((double) tempraw) / UINT16_MAX * 120.0 - 20.0)

/*!
 * @brief Converts Ah left to SoC in the range [0=-100%, 0xFFFF=200%]
 *
 * @param ampHour Current Ah left (double).
 * @param capacity Battery capacity in [Ah] (double).
 *
 * @return Converted value
 */
#define BCC_AMPHOUR_TO_SOC(ampHour, capacity) \
    ((uint16_t) ((((ampHour / capacity) + 1.0) / 3.0) * UINT16_MAX))

/*!
 * @brief Converts SoC (0 - 1) to SoC raw value [0=-100%, 0xFFFF=200%]
 *
 * @param SoC State of Charge (double).
 *
 * @return Converted value
 */
#define BCC_SOC_TO_SOCRAW(SoC) \
    ((uint16_t) (((double)(SoC) + 1.0) / 3.0 * UINT16_MAX))

/*!
 * @brief Converts SoC [0=-100%, 0xFFFF=200%] to Ah left
 *
 * @param SoC State of Charge raw value (uint16_t).
 * @param capacity Battery capacity in [Ah] (double).
 *
 * @return Amphere hour (double)
 */
#define BCC_SOC_TO_AMPHOUR(SoC, capacity) \
    ((double)(((double)SoC) / UINT16_MAX * 3.0 - 1.0) * capacity)

/*!
 * @brief Converts current (double) to (int16_t) raw value [-512A, 512A range]
 *
 * @param current Current in [A] (double).
 *
 * @return Current [raw] (int16_t)
 */
#define BCC_CURRENT_TO_RAW(current) \
    ((int16_t) (current * 64.0))

/*!
 * @brief Converts current (int16_t) raw value to [A] (double) [-512A, 512A range]
 *
 * @param current Current [raw] (int16_t).
 *
 * @return Current [A] (double)
 */
#define BCC_RAW_TO_CURRENT(raw) \
    (((double)raw) / 64.0)

/*******************************************************************************
 * Macros for conversion of the voltage thresholds
 ******************************************************************************/

/*!
 * @brief Extracts a value from Over Coulomb counting threshold to be placed in
 * the TH_COULOMB_CNT_MSB register.
 *
 * @param threshold Threshold value (signed two's complement, with V_2RES
 *                  resolution).
 */
#define BCC_GET_TH_COULOMB_CNT_MSB(threshold) \
    ((uint16_t)((threshold) >> 16U))

/*!
 * @brief Extracts a value from Over Coulomb counting threshold to be placed in
 * the TH_COULOMB_CNT_LSB register.
 *
 * @param threshold Threshold value (signed two's complement, with V_2RES
 *                  resolution).
 */
#define BCC_GET_TH_COULOMB_CNT_LSB(threshold) \
    ((uint16_t)((threshold) && 0xFFFF))

/*!
 * @brief Converts an overcurrent threshold voltage (in sleep mode) to a raw
 * value to be placed in the TH_ISENSE_OC register.
 *
 * @param threshold Threshold value in [uV].
 */
#define BCC_GET_TH_ISENSE_OC(threshold) \
    (((((threshold) * 5U) / 6U) > 0xFFFU) ? 0xFFFU : (((threshold) * 5U) / 6U))

/*!
 * @brief Converts a cell terminal OV/UV threshold voltage to a raw value to be
 * placed in appropriate bit fields of TH_ALL_CT and TH_CTx registers.
 *
 * Note that value of OV_UV_EN[COMMON_UV_TH] and OV_UV_EN[COMMON_OV_TH] bits
 * determines whether thresholds stored in TH_ALL_CT register are applied or
 * individual thresholds stored in TH_CTx registers are applied.
 *
 * @param threshold Threshold value in [mV].
 */
#define BCC_GET_TH_CTX(threshold) \
    (uint16_t)(((((threshold) * 10U) / 195U) > 0xFF) ? 0xFF : (((threshold) * 10U) / 195U))

/*!
 * @brief Converts an analog input OV/UV (UT/OT) threshold voltage to a raw
 * value to be placed in TH_ANx_OT and TH_ANx_UT registers.
 *
 * @param threshold Threshold value in [mV].
 */
#define BCC_GET_TH_ANX(threshold) \
    (uint16_t)((((((uint32_t)(threshold)) * 100U) / 488U) > 0x3FFU) ? 0x3FFU : ((((uint32_t)(threshold)) * 100U) / 488U))

/*******************************************************************************
 * User definitions
 ******************************************************************************/

/* The following macros determine how the SPI data buffers (arrays) passed to
 * BCC_MCU_TransferTpl and BCC_MCU_TransferSpi functions are organized.
 * Specifically, the macros influence the byte order in the buffers and
 * alignment of SPI frames in the buffers.
 *
 * Note that the natural assignment (for big-endian) is as follows:
 * #define BCC_MSG_SIZE              6U (48b frames)
 *
 * #define BCC_MSG_IDX_DATA_H        0U (first sent/received byte)
 * #define BCC_MSG_IDX_DATA_L        1U
 * #define BCC_MSG_IDX_ADDR          2U
 * #define BCC_MSG_IDX_CID           3U
 * #define BCC_MSG_IDX_CNT_CMD       4U
 * #define BCC_MSG_IDX_CRC           5U (last sent/received byte)
 *
 * Different values can be useful in order to fit low-level drivers of various
 * MCUs (e.g. big-endian vs. little-endian). The default configuration in this
 * BCC driver (as it follows) is determined for S32K1xx SDK 3.0.0 RTM. Note that
 * LPSPI driver in S32K1xx SDK requires alignment of the buffer to multiples of
 * four. Therefore, BCC_MSG_SIZE is eight instead of six. */

/*! @brief Number of consecutive bytes reserved for each SPI frame in buffers
 * passed to BCC_MCU_Transfer* functions (i.e. SPI frame alignment). */
#define BCC_MSG_SIZE 6U

/*! @brief Index to Register data byte (higher byte) of SPI frame in the
 * buffers passed to BCC_MCU_Transfer* functions.
 * Note: This byte should be the first sent to (received from) the SPI bus. */
#define BCC_MSG_IDX_DATA_H 0U

/*! @brief Index to Register data byte (lower byte) of SPI frame in the buffers
 * passed to BCC_MCU_Transfer* functions.
 * Note: This byte should be the second sent to (received from) the SPI bus. */
#define BCC_MSG_IDX_DATA_L 1U

/*! @brief Index to Register address byte of SPI frame in the buffers passed to
 * BCC_MCU_Transfer* functions.
 * Note: This byte should be the third sent to (received from) the SPI bus. */
#define BCC_MSG_IDX_ADDR 2U

/*! @brief Index to Device address (CID) byte of SPI frame in the buffers
 * passed to BCC_MCU_Transfer* functions.
 * Note: This byte should be the fourth sent to (received from) the SPI bus. */
#define BCC_MSG_IDX_CID 3U

/*! @brief Index to Message counter and Command byte of SPI frame in the
 * buffers passed to BCC_MCU_Transfer* functions.
 * Note: This byte should be the fifth sent to (received from) the SPI bus. */
#define BCC_MSG_IDX_CNT_CMD 4U

/*! @brief Index to CRC byte of SPI frame in the buffers passed to
 * BCC_MCU_Transfer* functions.
 * Note: This byte should be the last sent to (received from) the SPI bus. */
#define BCC_MSG_IDX_CRC 5U

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*! @brief Maximal number of BCC devices in SPI mode. */
#define BCC_DEVICE_CNT_MAX_SPI 1U
/*! @brief Maximal number of BCC devices in TPL mode. */
#define BCC_DEVICE_CNT_MAX_TPL 63U
/*! @brief Maximal number of BCC devices. */
#define BCC_DEVICE_CNT_MAX BCC_DEVICE_CNT_MAX_TPL

/*! @brief Minimal battery cell count connected to MC33771C. */
#define MC33771C_MIN_CELLS 7U
/*! @brief Maximal battery cell count connected to MC33771C. */
#define MC33771C_MAX_CELLS 14U
/*! @brief Minimal battery cell count connected to MC33772C. */
#define MC33772C_MIN_CELLS 3U
/*! @brief Maximal battery cell count connected to MC33772C. */
#define MC33772C_MAX_CELLS 6U
/*! @brief Maximal battery cell count connected to any BCC device. */
#define BCC_MAX_CELLS 14U

/*! @brief SOC to data ready (includes post processing of data, ADC_CFG[AVG]=0)
 * (in [us]), typical value. */
#define BCC_T_EOC_TYP_US 520U

/*! @brief Timeout for SOC to data ready (ADC_CFG[AVG]=0) (in [us]).
 * Note: The typical value is 520 us, the maximal one 546 us. */
#define BCC_T_EOC_TIMEOUT_US 600U

/*! @brief Maximal battery cell count connected to the BCC device.
 *
 * @param dev BCC device type.
 */
#define BCC_MAX_CELLS_DEV(dev) \
    ((dev == BCC_DEVICE_MC33771C) ? MC33771C_MAX_CELLS : MC33772C_MAX_CELLS)

/*! @brief  Number of MC33771C measurement registers.
 *
 * Note MC33772C contains 22 measurement registers. For compliance
 * with bcc_measurements_t indexes, BCC_Meas_GetRawValues function
 * requires 30 x uint16_t array for both BCC devices.
 */
#define BCC_MEAS_CNT 30U

/*! @brief  Number of BCC status (fault) registers. */
#define BCC_STAT_CNT 10U

/*! @brief Max. number of frames that can be read at once in TPL mode. */
#define BCC_RX_LIMIT_TPL 0x7FU

/*! @brief Size of buffer that is used for receiving via SPI in TPL mode. */
#define BCC_RX_BUF_SIZE_TPL \
    (BCC_MSG_SIZE * (BCC_RX_LIMIT_TPL + 1U))

/*! @brief Number of GPIO/temperature sensor inputs. */
#define BCC_GPIO_INPUT_CNT 7U

/*!
 * Returns True if value VAL is in the range defined by MIN and MAX values
 * (range includes the border values).
 *
 * @param val Comparison value.
 * @param min Minimal value of the range.
 * @param max Maximal value of the range.
 *
 * @return True if value is the range. False otherwise.
 */
#define BCC_IS_IN_RANGE(val, min, max) (((val) >= (min)) && ((val) <= (max)))

/*!
 * @brief Returns a non-zero value when desired cell (cellNo) is connected
 * to the BCC specified by CID.
 *
 * @param bcc       BCC device pointer
 * @param cellNo    Cell number (range is {1, ..., 14} for MC33771C and
 *                  {1, ..., 6} for MC33772C).
 *
 * @return Non-zero value if cell is connected, zero otherwise.
 */
#define BCC_IS_CELL_CONN(bcc, cellNo) \
    (bcc->getCellMap() & (1U << ((cellNo)-1U)))

/*******************************************************************************
 * Macros for other configuration
 ******************************************************************************/

/*!
 * @brief Converts ADC2 offset value from [uV] to a raw value with 0.6 uV
 * resolution to be placed to ADC2_OFFSET_COMP bit-field of ADC2_OFFSET_COMP
 * register.
 *
 * @param offset Offset value in [uV] as 2's complement in int16_t.
 */
#define BCC_GET_ADC2_OFFSET(offset) \
    ((uint16_t)((((((int16_t)(offset)) * 10) / 6) > 127) ? 127 : ((((((int16_t)(offset)) * 10) / 6) < -128) ? -128 : ((((int16_t)(offset)) * 10) / 6))))

/*******************************************************************************
 * ENUMS
 ******************************************************************************/

/* Enum types definition. */
/*!
 * @addtogroup enum_group
 * @{
 */
/*! @brief Error codes. */
enum bcc_status_t
{
    BCC_STATUS_SUCCESS = 0U,         /*!< No error. */
    BCC_STATUS_PARAM_RANGE = 1U,     /*!< Parameter out of range. */
    BCC_STATUS_SPI_FAIL = 2U,        /*!< Fail in the SPI communication. */
    BCC_STATUS_COM_TIMEOUT = 3U,     /*!< Communication timeout. */
    BCC_STATUS_COM_ECHO = 4U,        /*!< Received "echo" frame from MC33664 does not correspond
                                          to the sent frame. */
    BCC_STATUS_COM_CRC = 5U,         /*!< Wrong CRC in the received SPI frame. */
    BCC_STATUS_COM_MSG_CNT = 6U,     /*!< Received frame has a valid CRC but the message counter
                                          value does not match to the expected one. */
    BCC_STATUS_COM_NULL = 7U,        /*!< Received frame has a valid CRC but all bit-fields
                                          except CRC and message counter are zero. This occurs only
                                          in SPI communication mode: during the very first message
                                          or as a response to an invalid request from MCU. */
    BCC_STATUS_DIAG_FAIL = 8U,       /*!< It is not allowed to enter diagnostic mode. */
    BCC_STATUS_DATA_RDY = 9U,       /*!< A new sequence of conversions is currently running. */
};

/*! @brief Cluster Identification Address.
 *
 * Note that SPI communication mode allows one cluster/device only.
 * The maximum number of clusters/devices for TPL mode is 63. */
enum bcc_cid_t
{
    BCC_CID_UNASSIG = 0U, /*!< ID of uninitialized BCC devices. */
    BCC_CID_DEV1 = 1U,    /*!< Cluster ID of device 1.
                               In SPI mode, it is the only connected device.
                               In TPL mode, it is the first device in daisy
                               chain (connected directly to MC33664). */
    BCC_CID_DEV2 = 2U,    /*!< Cluster ID of device 2. */
    BCC_CID_DEV3 = 3U,    /*!< Cluster ID of device 3. */
    BCC_CID_DEV4 = 4U,    /*!< Cluster ID of device 4. */
    BCC_CID_DEV5 = 5U,    /*!< Cluster ID of device 5. */
    BCC_CID_DEV6 = 6U,    /*!< Cluster ID of device 6. */
    BCC_CID_DEV7 = 7U,    /*!< Cluster ID of device 7. */
    BCC_CID_DEV8 = 8U,    /*!< Cluster ID of device 8. */
    BCC_CID_DEV9 = 9U,    /*!< Cluster ID of device 9. */
    BCC_CID_DEV10 = 10U,  /*!< Cluster ID of device 10. */
    BCC_CID_DEV11 = 11U,  /*!< Cluster ID of device 11. */
    BCC_CID_DEV12 = 12U,  /*!< Cluster ID of device 12. */
    BCC_CID_DEV13 = 13U,  /*!< Cluster ID of device 13. */
    BCC_CID_DEV14 = 14U,  /*!< Cluster ID of device 14. */
    BCC_CID_DEV15 = 15U,  /*!< Cluster ID of device 15. */
    BCC_CID_DEV16 = 16U,  /*!< Cluster ID of device 16. */
    BCC_CID_DEV17 = 17U,  /*!< Cluster ID of device 17. */
    BCC_CID_DEV18 = 18U,  /*!< Cluster ID of device 18. */
    BCC_CID_DEV19 = 19U,  /*!< Cluster ID of device 19. */
    BCC_CID_DEV20 = 20U,  /*!< Cluster ID of device 20. */
    BCC_CID_DEV21 = 21U,  /*!< Cluster ID of device 21. */
    BCC_CID_DEV22 = 22U,  /*!< Cluster ID of device 22. */
    BCC_CID_DEV23 = 23U,  /*!< Cluster ID of device 23. */
    BCC_CID_DEV24 = 24U,  /*!< Cluster ID of device 24. */
    BCC_CID_DEV25 = 25U,  /*!< Cluster ID of device 25. */
    BCC_CID_DEV26 = 26U,  /*!< Cluster ID of device 26. */
    BCC_CID_DEV27 = 27U,  /*!< Cluster ID of device 27. */
    BCC_CID_DEV28 = 28U,  /*!< Cluster ID of device 28. */
    BCC_CID_DEV29 = 29U,  /*!< Cluster ID of device 29. */
    BCC_CID_DEV30 = 30U,  /*!< Cluster ID of device 30. */
    BCC_CID_DEV31 = 31U,  /*!< Cluster ID of device 31. */
    BCC_CID_DEV32 = 32U,  /*!< Cluster ID of device 32. */
    BCC_CID_DEV33 = 33U,  /*!< Cluster ID of device 33. */
    BCC_CID_DEV34 = 34U,  /*!< Cluster ID of device 34. */
    BCC_CID_DEV35 = 35U,  /*!< Cluster ID of device 35. */
    BCC_CID_DEV36 = 36U,  /*!< Cluster ID of device 36. */
    BCC_CID_DEV37 = 37U,  /*!< Cluster ID of device 37. */
    BCC_CID_DEV38 = 38U,  /*!< Cluster ID of device 38. */
    BCC_CID_DEV39 = 39U,  /*!< Cluster ID of device 39. */
    BCC_CID_DEV40 = 40U,  /*!< Cluster ID of device 40. */
    BCC_CID_DEV41 = 41U,  /*!< Cluster ID of device 41. */
    BCC_CID_DEV42 = 42U,  /*!< Cluster ID of device 42. */
    BCC_CID_DEV43 = 43U,  /*!< Cluster ID of device 43. */
    BCC_CID_DEV44 = 44U,  /*!< Cluster ID of device 44. */
    BCC_CID_DEV45 = 45U,  /*!< Cluster ID of device 45. */
    BCC_CID_DEV46 = 46U,  /*!< Cluster ID of device 46. */
    BCC_CID_DEV47 = 47U,  /*!< Cluster ID of device 47. */
    BCC_CID_DEV48 = 48U,  /*!< Cluster ID of device 48. */
    BCC_CID_DEV49 = 49U,  /*!< Cluster ID of device 49. */
    BCC_CID_DEV50 = 50U,  /*!< Cluster ID of device 50. */
    BCC_CID_DEV51 = 51U,  /*!< Cluster ID of device 51. */
    BCC_CID_DEV52 = 52U,  /*!< Cluster ID of device 52. */
    BCC_CID_DEV53 = 53U,  /*!< Cluster ID of device 53. */
    BCC_CID_DEV54 = 54U,  /*!< Cluster ID of device 54. */
    BCC_CID_DEV55 = 55U,  /*!< Cluster ID of device 55. */
    BCC_CID_DEV56 = 56U,  /*!< Cluster ID of device 56. */
    BCC_CID_DEV57 = 57U,  /*!< Cluster ID of device 57. */
    BCC_CID_DEV58 = 58U,  /*!< Cluster ID of device 58. */
    BCC_CID_DEV59 = 59U,  /*!< Cluster ID of device 59. */
    BCC_CID_DEV60 = 60U,  /*!< Cluster ID of device 60. */
    BCC_CID_DEV61 = 61U,  /*!< Cluster ID of device 61. */
    BCC_CID_DEV62 = 62U,  /*!< Cluster ID of device 62. */
    BCC_CID_DEV63 = 63U   /*!< Cluster ID of device 63. */
};

/*! @brief BCC device.  */
enum bcc_device_t
{
    BCC_DEVICE_MC33771C = 0U, /*!< MC33771C. */
    BCC_DEVICE_MC33772C = 1U  /*!< MC33772C. */
};

/*! @brief Measurements provided by battery cell controller.
 *
 * Note that MC33772C doesn't have MEAS_CELL7 - MEAS_CELL14 registers.
 * Function BCC_Meas_GetRawValues returns 0x0000 at these positions.
 */
enum bcc_measurements_t
{
    BCC_MSR_CC_NB_SAMPLES = 0U, /*!< Number of samples in Coulomb counter (register CC_NB_SAMPLES). */
    BCC_MSR_COULOMB_CNT1 = 1U,  /*!< Coulomb counting accumulator (register COULOMB_CNT1). */
    BCC_MSR_COULOMB_CNT2 = 2U,  /*!< Coulomb counting accumulator (register COULOMB_CNT2). */
    BCC_MSR_ISENSE1 = 3U,       /*!< ISENSE measurement (register MEAS_ISENSE1). */
    BCC_MSR_ISENSE2 = 4U,       /*!< ISENSE measurement (register MEAS_ISENSE2). */
    BCC_MSR_STACK_VOLT = 5U,    /*!< Stack voltage measurement (register MEAS_STACK). */
    BCC_MSR_CELL_VOLT14 = 6U,   /*!< Cell 14 voltage measurement (register MEAS_CELL14). */
    BCC_MSR_CELL_VOLT13 = 7U,   /*!< Cell 13 voltage measurement (register MEAS_CELL13). */
    BCC_MSR_CELL_VOLT12 = 8U,   /*!< Cell 12 voltage measurement (register MEAS_CELL12). */
    BCC_MSR_CELL_VOLT11 = 9U,   /*!< Cell 11 voltage measurement (register MEAS_CELL11). */
    BCC_MSR_CELL_VOLT10 = 10U,  /*!< Cell 10 voltage measurement (register MEAS_CELL10). */
    BCC_MSR_CELL_VOLT9 = 11U,   /*!< Cell 9 voltage measurement (register MEAS_CELL9). */
    BCC_MSR_CELL_VOLT8 = 12U,   /*!< Cell 8 voltage measurement (register MEAS_CELL8). */
    BCC_MSR_CELL_VOLT7 = 13U,   /*!< Cell 7 voltage measurement (register MEAS_CELL7). */
    BCC_MSR_CELL_VOLT6 = 14U,   /*!< Cell 6 voltage measurement (register MEAS_CELL6). */
    BCC_MSR_CELL_VOLT5 = 15U,   /*!< Cell 5 voltage measurement (register MEAS_CELL5). */
    BCC_MSR_CELL_VOLT4 = 16U,   /*!< Cell 4 voltage measurement (register MEAS_CELL4). */
    BCC_MSR_CELL_VOLT3 = 17U,   /*!< Cell 3 voltage measurement (register MEAS_CELL3). */
    BCC_MSR_CELL_VOLT2 = 18U,   /*!< Cell 2 voltage measurement (register MEAS_CELL2). */
    BCC_MSR_CELL_VOLT1 = 19U,   /*!< Cell 1 voltage measurement (register MEAS_CELL1). */
    BCC_MSR_AN6 = 20U,          /*!< Analog input 6 voltage measurement (register MEAS_AN6). */
    BCC_MSR_AN5 = 21U,          /*!< Analog input 5 voltage measurement (register MEAS_AN5). */
    BCC_MSR_AN4 = 22U,          /*!< Analog input 4 voltage measurement (register MEAS_AN4). */
    BCC_MSR_AN3 = 23U,          /*!< Analog input 3 voltage measurement (register MEAS_AN3). */
    BCC_MSR_AN2 = 24U,          /*!< Analog input 2 voltage measurement (register MEAS_AN2). */
    BCC_MSR_AN1 = 25U,          /*!< Analog input 1 voltage measurement (register MEAS_AN1). */
    BCC_MSR_AN0 = 26U,          /*!< Analog input 0 voltage measurement (register MEAS_AN0). */
    BCC_MSR_ICTEMP = 27U,       /*!< IC temperature measurement (register MEAS_IC_TEMP). */
    BCC_MSR_VBGADC1A = 28U,     /*!< ADCIA Band Gap Reference measurement (register MEAS_VBG_DIAG_ADC1A). */
    BCC_MSR_VBGADC1B = 29U      /*!< ADCIB Band Gap Reference measurement (register MEAS_VBG_DIAG_ADC1B). */
};

/*! @brief Number of samples to be averaged in the conversion request. */
enum bcc_avg_t
{
    BCC_AVG_1 = MC33771C_ADC_CFG_AVG_NO_AVERAGING_ENUM_VAL,  /*!< No averaging, the result is taken as is. */
    BCC_AVG_2 = MC33771C_ADC_CFG_AVG_2_SAMPLES_ENUM_VAL,     /*!< Averaging of 2 consecutive samples. */
    BCC_AVG_4 = MC33771C_ADC_CFG_AVG_4_SAMPLES_ENUM_VAL,     /*!< Averaging of 4 consecutive samples. */
    BCC_AVG_8 = MC33771C_ADC_CFG_AVG_8_SAMPLES_ENUM_VAL,     /*!< Averaging of 8 consecutive samples. */
    BCC_AVG_16 = MC33771C_ADC_CFG_AVG_16_SAMPLES_ENUM_VAL,   /*!< Averaging of 16 consecutive samples. */
    BCC_AVG_32 = MC33771C_ADC_CFG_AVG_32_SAMPLES_ENUM_VAL,   /*!< Averaging of 32 consecutive samples. */
    BCC_AVG_64 = MC33771C_ADC_CFG_AVG_64_SAMPLES_ENUM_VAL,   /*!< Averaging of 64 consecutive samples. */
    BCC_AVG_128 = MC33771C_ADC_CFG_AVG_128_SAMPLES_ENUM_VAL, /*!< Averaging of 126 consecutive samples. */
    BCC_AVG_256 = MC33771C_ADC_CFG_AVG_256_SAMPLES_ENUM_VAL  /*!< Averaging of 256 consecutive samples. */
};

/*! @brief Status provided by battery cell controller. */
enum bcc_fault_status_t
{
    BCC_FS_CELL_OV = 0U,     /*!< CT overvoltage fault (register CELL_OV_FLT). */
    BCC_FS_CELL_UV = 1U,     /*!< CT undervoltage fault (register CELL_UV_FLT). */
    BCC_FS_CB_OPEN = 2U,     /*!< Open CB fault (register CB_OPEN_FLT). */
    BCC_FS_CB_SHORT = 3U,    /*!< Short CB fault (register CB_SHORT_FLT). */
    BCC_FS_AN_OT_UT = 4U,    /*!< AN over and undertemperature (register AN_OT_UT_FLT). */
    BCC_FS_GPIO_SHORT = 5U,  /*!< Short GPIO/open AN diagnostic (register GPIO_SHORT_ANx_OPEN_STS). */
    BCC_FS_COMM = 6U,        /*!< Number of communication errors detected (register COM_STATUS). */
    BCC_FS_FAULT1 = 7U,      /*!< Fault status (register FAULT1_STATUS). */
    BCC_FS_FAULT2 = 8U,      /*!< Fault status (register FAULT2_STATUS). */
    BCC_FS_FAULT3 = 9U      /*!< Fault status (register FAULT3_STATUS). */
};

/*! @brief Mode of a GPIOx/ANx pin. */
enum bcc_pin_mode_t
{
    BCC_PIN_ANALOG_IN_RATIO = 0U, /*!< Analog input for ratiometric measurement). */
    BCC_PIN_ANALOG_IN_ABS = 1U,   /*!< Analog input for absolute measurement. */
    BCC_PIN_DIGITAL_IN = 2U,      /*!< Digital input. */
    BCC_PIN_DIGITAL_OUT = 3U,     /*!< Digital output. */
    BCC_PIN_WAKE_UP_IN = 4U,      /*!< Digital input with a wake-up capability (only GPIO0).
                                       Wakes-up BCC from sleep to normal mode on any edge of GPIO0. */
    BCC_PIN_CONVERT_TR_IN = 5U    /*!< Digital input with a convert trigger capability (only GPIO2).
                                       A rising edge on GPIO2 triggers an ADC1-A and ADC1-B
                                       conversion when BCC is in normal mode. */
};

/*! @} */

/* Configure struct types definition. */
/*!
 * @addtogroup struct_group
 * @{
 */
/*! @brief Coulomb counter data. */
struct bcc_cc_data_t
{
    int32_t ccAccumulator; /*!< Coulomb counting accumulator with V_2RES resolution. */
    uint16_t nbSamples;    /*!< Number of samples accumulated in the Coulomb counter. */
};

/*!
 * @brief Driver internal data.
 *
 * Note that it is initialized in BCC_Init function by the driver
 * and the user mustn't change it at any time.
 */
struct bcc_drv_data_t
{
    uint16_t cellMap[BCC_DEVICE_CNT_MAX]; /*!< Bit map of used cells of each BCC device. */ // MOVED TO BCC
    uint8_t msgCntr[BCC_DEVICE_CNT_MAX + 1];                                                /*!< Last received value of Message counter (values 0-15). // MOVED TO COMM
                                                                                                 MsgCntr[0] contains Message counter of CID=0. */
    uint8_t rxBuf[BCC_RX_BUF_SIZE_TPL]; /*!< Buffer for receiving data in TPL mode. */      // MOVED TO COMM
};

/*!
 * @brief Driver configuration.
 *
 * This structure contains all information needed for proper functionality of
 * the driver, such as used communication mode, BCC device(s) configuration or
 * internal driver data.
 */
struct bcc_drv_config_t
{
    uint8_t devicesCnt;                      /*!< Number of BCC devices. SPI mode allows one device only, // MOVED TO CONTROLLER
                                                  TPL mode allows up to 63 devices. */
    bcc_device_t device[BCC_DEVICE_CNT_MAX]; /*!< BCC device type of // MOVED TO BCC
                                                  [0] BCC with CID=1, [1] BCC with CID=2, etc. */
    uint16_t cellCnt[BCC_DEVICE_CNT_MAX];    /*!< Number of connected cells to each BCC. // MOVED TO BCC
                                                  [0] BCC with CID=1, [1] BCC with CID=2, etc. */
    bool loopBack;                           /*!< Loop back mode. If False, TPL_TX_Term. (RDTX_OUT) bit // MOVED TO CONTROLLER
                                                  is set for the last BCC device in the TPL chain.
                                                  This configuration item is ignored in SPI mode. */

    bcc_drv_data_t drvData; /*!< Internal driver data. */
};

/*!
 *   @brief Initial register value struct
 */
struct bcc_init_reg_t
{
    const uint8_t address;
    const uint16_t defaultVal;
    uint16_t value;
};
/*******************************************************************************
 * EOF;
 ******************************************************************************/

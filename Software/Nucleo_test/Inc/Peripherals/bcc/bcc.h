/*!
 * @file bcc.h
 *
 * Battery cell controller SW driver for MC33771C and MC33772C v2.2.
 */

/*

BCC_AssignCid -> BCC                                    - Arjan
BCC_InitDevices -> slaveController                      - Arjan
BCC_SetGpioCfg -> BCC                                   - Arjan
BCC_Init -> SlaveController                             - Arjan
BCC_Sleep -> slaveController                            - Arjan
BCC_SoftwareReset -> bcc                                - Bram
BCC_Meas_StartConversion -> BCC                         - Bram
BCC_Meas_StartConversionGlobal -> SlaveController       - Arjan
BCC_Meas_IsConverting -> BCC                            - Bram
BCC_Meas_StartAndWait -> BCC                            - Bram
BCC_Meas_GetRawValues -> BCC                            - Bram
BCC_Meas_GetCoulombCounter -> BCC                       - Bram
BCC_Meas_GetIsenseVoltage -> BCC                        - Bram
BCC_Meas_GetStackVoltage -> BCC                         - Bram
BCC_Meas_GetCellVoltages -> BCC                         - Bram
BCC_Meas_GetCellVoltage -> BCC                          - Bram
BCC_Meas_GetAnVoltages -> BCC                           - Bram
BCC_Meas_GetAnVoltage -> BCC                            - Bram
BCC_Meas_GetIcTemperature -> BCC                        - Bram
BCC_Fault_GetStatus -> BCC                              - Bram
BCC_Fault_ClearStatus -> BCC                            - Bram
BCC_GPIO_SetMode -> BCC                                 - Bram
BCC_GPIO_ReadPin -> BCC                                 - Bram
BCC_GPIO_SetOutput -> BCC                               - Bram
BCC_CB_Enable -> BCC                                    - Bram
BCC_CB_SetIndividual -> BCC                             - Bram
BCC_CB_Pause -> BCC                                     - Bram
BCC_FuseMirror_Read -> BCC                              - Bram
BCC_FuseMirror_Write -> BCC                             - Bram
BCC_GUID_Read -> BCC                                    - Bram


*/

#pragma once

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include <stdbool.h>
#include <stddef.h>
#include "bcc_utils.h"
#include "bcc_communication.h"
#include "main.h"
#include "SPIwrapper.h"

class BCC
{
private:
    /*******************************************************************************
     * DEVICE_TYPE SPECIFIC VARIABLES
     ******************************************************************************/

    // Device type
    const bcc_device_t mDevice;

    // Number of connected cells
    const uint8_t mCellCount;

    // Number of connected NTCs
    const uint8_t mNTCCount;

    // Current sensing enabled
    const bool mCurrentSenseEnabled;

    // CID
    bcc_cid_t mCID;

    // Map of connected cells
    const uint16_t mCellMap;

    // Message count
    uint8_t mMsgCnt = 0; /*!< Last received value of Message counter (values 0-15). */

    /*******************************************************************************
     * PRIVATE FUNCTIONS
     ******************************************************************************/

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
    bcc_status_t setGpioCfg(const uint8_t gpioSel, const bcc_pin_mode_t mode);

    
public:
    BCC(bcc_device_t device, uint8_t cellCount, uint8_t ntcCount, bool currentSenseEnabled, bcc_cid_t cid);

    /*******************************************************************************
     * PUBLIC FUNCTIONS - REGISTER OPERATIONS
     ******************************************************************************/

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
    bcc_status_t regRead(const uint8_t regAddr, const uint8_t regCnt, uint16_t *regVal, const bool toUnassigned = false);

    /*!
     * @brief This function writes a value to addressed register of the BCC device.
     *
     * @param regAddr   Register address. See MC33771C.h and MC33772C.h header files
     *                  for possible values (MC3377*C_*_OFFSET macros).
     * @param regVal    New value of selected register.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t regWrite(const uint8_t regAddr, const uint16_t regVal, const bool toUnassigned = false);

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
    static bcc_status_t regWriteGlobal(const uint8_t regAddr, const uint16_t regVal);

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
    bcc_status_t regUpdate(const uint8_t regAddr, const uint16_t regMask, const uint16_t regVal);

    /*!
     * @brief This function sends a No Operation command to the BCC device.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t sendNop();

    /*!
     * @brief This function check the message count of the BCC device.
     *
     * @param resp       Response to check.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t checkMsgCnt(const uint8_t *const resp);

    /*******************************************************************************
     * PUBLIC FUNCTIONS - API
     ******************************************************************************/

    /*!
     * @brief This function assigns CID to a BCC device that has CID equal to zero.
     * It also stores the MsgCntr value for appropriate CID, terminates the RDTX_OUT
     * of the last node if loop-back is not required (in the TPL mode) and checks
     * if the node with newly set CID replies.
     *
     * @param drvConfig Pointer to driver instance configuration.
     * @param cid       Cluster ID to be set to the BCC device with CID=0.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t assignCid(bool loopback, uint8_t devicesCnt);


    /*!
     * @brief This function resets the BCC device using software reset. It enters reset
     * via SPI or TPL interface.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t softwareReset();

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
    bcc_status_t meas_StartConversion(const bcc_avg_t avg);



    /*!
     * @brief This function checks status of conversion defined by End of Conversion
     * bit in ADC_CFG register.
     *
     * @param completed Pointer to check result. True if a conversion is complete.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t meas_IsConverting(bool *const completed);

    /*!
     * @brief This function starts an on-demand conversion in selected BCC device
     * and waits for completion.
     *
     * @param avg       Number of samples to be averaged.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t meas_StartAndWait(const bcc_avg_t avg);

    /*!
     * @brief This function reads the measurement registers and returns raw values.
     *
     * Macros defined in bcc_utils.h can be used to perform correct unit conversion.
     *
     * Warning: The "data ready" flag is ignored.
     *
     * @param measurements Array containing all values measured by BCC. Size of this
     *                     array must be BCC_MEAS_CNT*16b. Indexes into the array
     *                     are defined in enumeration bcc_measurements_t
     *                     placed in this header file.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t meas_GetRawValues(uint16_t *const measurements);

    /*!
     * @brief This function reads the Coulomb counter registers.
     *
     * Note that the Coulomb counter is independent on the on-demand conversions.
     *
     * @param cc        Coulomb counter data.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t meas_GetCoulombCounter(bcc_cc_data_t *const cc);

    /*!
     * @brief This function reads the ISENSE measurement and converts it to [uV].
     *
     * @param isenseVolt Pointer to memory where the ISENSE voltage (in [uV]) will
     *                   be stored.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t meas_GetIsenseVoltage(int32_t *const isenseVolt);

    /*!
     * @brief This function reads the stack measurement and converts it to [uV].
     *
     * @param stackVolt Pointer to memory where the stack voltage (in [uV]) will
     *                  be stored.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t meas_GetStackVoltage(uint32_t *const stackVolt);

    /*!
     * @brief This function reads the cell measurements and converts them to [uV].
     *
     * @param cellVolt  Pointer to the array where the cell voltages (in [uV]) will
     *                  be stored. Array must have an suitable size (14*32b for
     *                  MC33771C and 6*32b for MC33772C). Cell 1 voltage is stored
     *                  at [0], Cell 2 voltage at [1], etc.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t meas_GetCellVoltages(uint32_t *const cellVolt);

    /*!
     * @brief This function reads the voltage measurement of a selected cell and
     * converts it to [uV].
     *
     * @param cellIndex Cell index. Use 0U for CELL 1, 1U for CELL2, etc.
     * @param cellVolt  Pointer to memory where the cell voltage (in [uV]) will
     *                  be stored.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t meas_GetCellVoltage(uint8_t cellIndex, uint32_t *const cellVolt);

    /*!
     * @brief This function reads the voltage measurement for all ANx and converts
     * them to [uV]. Intended for ANx configured for absolute measurements only!
     *
     * @param anVolt    Pointer to the array where ANx voltages (in [uV]) will
     *                  be stored. Array must have an suitable size
     *                  (BCC_GPIO_INPUT_CNT * 32b). AN0 voltage is stored at [0],
     *                  AN1 at [1], etc.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t meas_GetAnVoltages(uint32_t *const anVolt);

    /*!
     * @brief This function reads the voltage measurement of a selected ANx and
     * converts it to [uV]. Intended for ANx configured for absolute measurements
     * only!
     *
     * @param anIndex   ANx index. Use 0U for AN0, 1U for AN1, etc.
     * @param anVolt    Pointer to memory where the ANx voltage (in [uV]) will
     *                  be stored.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t meas_GetAnVoltage(uint8_t anIndex, uint32_t *const anVolt);

    /*!
     * @brief This function reads the BCC temperature and converts it to the
     * selected unit.
     *
     * @param unit      Temperature unit.
     * @param icTemp    Pointer to memory where the IC temperature will be stored.
     *                  Resolution is 0.1; unit is selected by parameter "unit".
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t meas_GetIcTemperature(bcc_temp_unit_t unit, int16_t *const icTemp);

    /*!
     * @brief This function reads the fault status registers of the BCC device.
     *
     * @param fltStatus Array containing all fault status information provided by
     *                  BCC. Required size of the array is BCC_STAT_CNT*16b.
     *                  Indexes into the array are defined in bcc_fault_status_t
     *                  enumeration placed in this header file.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t fault_GetStatus(uint16_t *const fltStatus);

    /*!
     * @brief This function clears selected fault status register.
     *
     * @param statSel   Selection of a fault status register to be cleared. See
     *                  definition of this enumeration in this header file.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t fault_ClearStatus(const bcc_fault_status_t statSel);

    /*!
     * @brief This function sets the mode of one BCC GPIOx/ANx pin.
     *
     * @param gpioSel   Index of pin to be configured. Index starts at 0 (GPIO0).
     * @param mode      Pin mode.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t GPIO_SetMode(const uint8_t gpioSel, const bcc_pin_mode_t mode);

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
    bcc_status_t GPIO_ReadPin(const uint8_t gpioSel, bool *const val);

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
    bcc_status_t GPIO_SetOutput(const uint8_t gpioSel, const bool val);

    /*!
     * @brief This function enables or disables the cell balancing via
     * SYS_CFG1[CB_DRVEN] bit.
     *
     * Note that each cell balancing driver needs to be setup separately, e.g. by
     * CB_SetIndividual function.
     *
     * @param enable    State of cell balancing. False (cell balancing disabled) or
     *                  True (cell balancing enabled).
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t CB_Enable(const bool enable);

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
    bcc_status_t CB_SetIndividual(const uint8_t cellIndex, const bool enable,
                                      const uint16_t timer);

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
    bcc_status_t CB_Pause(const bool pause);

    /*!
     * @brief This function reads a fuse mirror register of selected BCC device.
     *
     * @param fuseAddr  Address of a fuse mirror register to be read.
     * @param value     Pointer to memory where the read value will be stored.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t fuseMirror_Read(const uint8_t fuseAddr, uint16_t *const value);

    /*!
     * @brief This function writes a fuse mirror register of selected BCC device.
     *
     * @param fuseAddr  Address of a fuse mirror register to be written.
     * @param value     Value to be written.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t fuseMirror_Write(const uint8_t fuseAddr, const uint16_t value);

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

    bcc_status_t GUID_Read(uint64_t *const guid);

    /**
    * Get the device type, is either MC33771C or MC33772C
    */
    inline bcc_device_t getDeviceType() { return mDevice; }

    /**
    * Get the cell mapping of the connected cells
    */
    inline uint16_t getCellMap() { return mCellMap; }

    /**
    * Get the CID of this BCC
    */
    inline bcc_cid_t getCID() { return mCID;}

    /**
    * Check if the BCC has a valid configuration
    */
    bool hasValidConfig();

    /**
     * Returns true if current sensing is enabled. False if disabled
    */
    bool currentSenseEnabled();

    /**
     * Returns the number of connected cells
    */
    uint8_t getCellCount();
    
    /**
     * Returns the number of connected NTCs
    */
    uint8_t getNTCCount();
};
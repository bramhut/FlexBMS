/*!
 * @file bcc.h
 *
 * Battery cell controller SW driver for MC33771C and MC33772C v2.2.
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
#include <vector>

class BCC
{
public:
    /*******************************************************************************
     * TYPE DEFINITIONS
     ******************************************************************************/

    /**
     * Struct representing the raw measurements of the BCC
     * Warning: order and alignment of the variables is important
     * and should be in the same order as the BCC registers
     * see bcc_measurements_t as reference
     */

    enum bcc_measurements_t
    {
        MSR_CC_NB_SAMPLES = 0U, /*!< Number of samples in Coulomb counter (register CC_NB_SAMPLES). */
        MSR_COULOMB_CNT1 = 1U,  /*!< Coulomb counting accumulator (register COULOMB_CNT1). */
        MSR_COULOMB_CNT2 = 2U,  /*!< Coulomb counting accumulator (register COULOMB_CNT2). */
        MSR_ISENSE1 = 3U,       /*!< ISENSE measurement (register MEAS_ISENSE1). */
        MSR_ISENSE2 = 4U,       /*!< ISENSE measurement (register MEAS_ISENSE2). */
        MSR_STACK_VOLT = 5U,    /*!< Stack voltage measurement (register MEAS_STACK). */
        MSR_CELL_VOLT14 = 6U,   /*!< Cell 14 voltage measurement (register MEAS_CELL14). */
        MSR_CELL_VOLT13 = 7U,   /*!< Cell 13 voltage measurement (register MEAS_CELL13). */
        MSR_CELL_VOLT12 = 8U,   /*!< Cell 12 voltage measurement (register MEAS_CELL12). */
        MSR_CELL_VOLT11 = 9U,   /*!< Cell 11 voltage measurement (register MEAS_CELL11). */
        MSR_CELL_VOLT10 = 10U,  /*!< Cell 10 voltage measurement (register MEAS_CELL10). */
        MSR_CELL_VOLT9 = 11U,   /*!< Cell 9 voltage measurement (register MEAS_CELL9). */
        MSR_CELL_VOLT8 = 12U,   /*!< Cell 8 voltage measurement (register MEAS_CELL8). */
        MSR_CELL_VOLT7 = 13U,   /*!< Cell 7 voltage measurement (register MEAS_CELL7). */
        MSR_CELL_VOLT6 = 14U,   /*!< Cell 6 voltage measurement (register MEAS_CELL6). */
        MSR_CELL_VOLT5 = 15U,   /*!< Cell 5 voltage measurement (register MEAS_CELL5). */
        MSR_CELL_VOLT4 = 16U,   /*!< Cell 4 voltage measurement (register MEAS_CELL4). */
        MSR_CELL_VOLT3 = 17U,   /*!< Cell 3 voltage measurement (register MEAS_CELL3). */
        MSR_CELL_VOLT2 = 18U,   /*!< Cell 2 voltage measurement (register MEAS_CELL2). */
        MSR_CELL_VOLT1 = 19U,   /*!< Cell 1 voltage measurement (register MEAS_CELL1). */
        MSR_AN6 = 20U,          /*!< Analog input 6 voltage measurement (register MEAS_AN6). */
        MSR_AN5 = 21U,          /*!< Analog input 5 voltage measurement (register MEAS_AN5). */
        MSR_AN4 = 22U,          /*!< Analog input 4 voltage measurement (register MEAS_AN4). */
        MSR_AN3 = 23U,          /*!< Analog input 3 voltage measurement (register MEAS_AN3). */
        MSR_AN2 = 24U,          /*!< Analog input 2 voltage measurement (register MEAS_AN2). */
        MSR_AN1 = 25U,          /*!< Analog input 1 voltage measurement (register MEAS_AN1). */
        MSR_AN0 = 26U,          /*!< Analog input 0 voltage measurement (register MEAS_AN0). */
        MSR_ICTEMP = 27U,       /*!< IC temperature measurement (register MEAS_IC_TEMP). */
        MSR_VBGADC1A = 28U,     /*!< ADCIA Band Gap Reference measurement (register MEAS_VBG_DIAG_ADC1A). */
        MSR_VBGADC1B = 29U      /*!< ADCIB Band Gap Reference measurement (register MEAS_VBG_DIAG_ADC1B). */
    };

    struct Config_t
    {
        bcc_device_t DEVICE_TYPE = BCC_DEVICE_MC33771C;
        uint8_t CELL_COUNT = 14;
        uint8_t NTC_COUNT = 0;
        bool CURRENT_SENSING_ENABLED = false;
        uint8_t AMPHOUR_BACKUP_REG = 0;
    };

    enum can_state_t
    {
        OK = 0U,
        COMM_ERR = 1U,
        DATA_RDY_ERR = 2U,
        PARAM_ERR = 3U,
    };

private:
    /*******************************************************************************
     * DEVICE_TYPE SPECIFIC SETTINGS
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
    const bcc_cid_t mCID;

    // Map of connected cells
    const uint16_t mCellMap;

    /*******************************************************************************
     * PRIVATE VARIABLES
     ******************************************************************************/

    // Short hand state of the device used in CAN messages
    can_state_t mCANstate = OK;
    static can_state_t mCANstateGlobal;

    // Raw measurements array, index it using bcc_measurements_t
    uint16_t mRawMeasurements[BCC_MEAS_CNT];

    // Raw status registers
    uint16_t mFaultStatusRegisters[BCC_STAT_CNT];

    // Indicates if the report communication error count has increased since last time register was fetched
    bool mCommunicationErrorCountIncreased = false;

    // Message count
    uint8_t mMsgCnt = 0; /*!< Last received value of Message counter (values 0-15). */

    // Ampere-hour registers in the RTC backup domain (data retained when VDD is off)
    size_t mAmpHourBackupReg;  // Register index of the first register of the pair
    volatile double *mAmpHour; // Pointer to the value

    // Coulomb counter difference variables
    uint16_t mCCPrevSamples = 0;
    uint32_t mCCPrevTime = 0;
    int32_t mCCPrevAccumulator = 0;

    // Current sensing variables
    double mIAvg = 0; // Average current [A]

    // Cell balancing information
    bool mBalancingList[14] = {false};
    bool mAnyCellBalancing = false;

    // Timing
    uint32_t mCBTimerEnd[14] = {0};               // Cell balancing end time array [s]
    uint32_t timeReceivedLastMeasurement = 0;     // Time when last measurement was received [ms]
    uint32_t mLastConversionStarted = 0;          // Time when last conversion was started [us]
    static uint32_t mLastGlobalConversionStarted; // Time when last global conversion was started [us]

    /*******************************************************************************
     * PRIVATE FUNCTIONS
     ******************************************************************************/

    /**
     * Update the balancing status of the cells
     */
    void updateBalancing();

    /*!
     * @brief Returns the channel index for a given cell index
     *
     * @param cellNo    Cell number (range is {1, ..., CONNECTED_CELLS}).
     *
     * @return Channel index
     */
    uint8_t getChannelIndex(const uint8_t cellNo);

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

    /*!
     * @brief This function maps the status to a CAN state
     *
     * @param state    Latest status
     */
    static can_state_t CANstateFromStatus(bcc_status_t status);

    /*!
     * @brief This function updates the CAN state if necessary. The CAN state is a
     * shorted version of the status used to send over CAN in 2 bits
     *
     * @param state    Latest status
     */
    void setCANstate(bcc_status_t status);

    /*!
     * @brief This function updates the CAN state if necessary. The CAN state is a
     * shorted version of the status used to send over CAN in 2 bits
     *
     * @param state    Latest status
     */
    static void setCANstateGlobal(bcc_status_t status);

    /*!
     * @brief Not all faults from FAULT1, FAULT2, FAULT3_STATUS are useful. This mask will remove useless faults
     *
     * @param faultValues fault array to be masked, starting at FAULT1
     */
    void applyFaultMasks(uint16_t *faultValues);

public:
    BCC(Config_t config, uint8_t cid);

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
    bcc_status_t checkMsgCnt(const uint8_t *const resp, bool toUnassigned);

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
    bcc_status_t assignCid(uint8_t devicesCnt);

    /*!
     * @brief This function resets the BCC device using software reset. It enters reset
     * via SPI or TPL interface.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t softwareReset();

    /*!
     * @brief This function resets the coulomb counter of the BCC device.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t resetBCCCoulombCounter();

    /*!
     * @brief This function resets the left ampere-hour counter (stored in NVM of uC)
     */
    void setAhCounter(double amphour);

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
     * @brief This function starts ADC conversion for all devices in TPL chain. It
     * uses a Global Write command to set ADC_CFG register.
     *
     * You can use function BCC_Meas_IsConverting to check conversion status and
     * function BCC_Meas_GetRawValues to retrieve the measured values.
     *
     * @return bcc_status_t Error code.
     */
    static bcc_status_t meas_StartConversionGlobal();

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
    bcc_status_t meas_WaitOnConversion(const bcc_avg_t avg);

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
     *
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t meas_GetRawValues();

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
    bcc_status_t meas_GetAmpHourAndIAvg(const double rShunt, const bool invertCurrent, double *const amphour, double *const Iavg, bool forceRead = false);

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
    bcc_status_t meas_GetIsense(const double rShunt, double *const isense, bool forceRead = false);

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
    bcc_status_t meas_GetStackVoltage(uint32_t *const stackVolt, bool forceRead = false);

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
    bcc_status_t meas_GetCellVoltages(std::vector<uint32_t> &cellVolt, bool forceRead = false);
    bcc_status_t meas_GetCellVoltages(uint32_t *const cellVolt, bool forceRead = false);

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
    bcc_status_t meas_GetCellVoltage(uint8_t cellIndex, uint32_t *const cellVolt, bool forceRead = false);

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
    bcc_status_t meas_GetAnVoltages(uint32_t *const anVolt, bool absVoltage = false, bool forceRead = false);

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
    bcc_status_t meas_GetAnVoltage(uint8_t anIndex, uint32_t *const anVolt, bool absVoltage = false, bool forceRead = false);

    /*!
     * @brief This function reads the internal BCC temperature
     *
     * @param icTemp    Pointer to memory where the IC temperature will be stored.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t meas_GetIcTemperature(uint16_t *const icTemp, bool forceRead = false);

    /*!
     * @brief This function reads the NTC's temperature
     *
     * @param temperatures    Pointer to vector where the temperatures will be stored.
     * @param NTCresistance   Resistance of the NTC in Ohms
     * @param NTCBeta         Beta value of the NTC
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t meas_GetNTCTemperatures(std::vector<uint16_t> &temperatures, double NTCresistance, double NTCBeta, bool forceRead = false);

    /*!
     * @brief This function reads the fault status registers of the BCC device. Will clear the fault status registers.
     *
     * @param allFaults Pointer to the array where the fault status will be stored. Will bitwise-OR all fault status registers with allFaults.
     */
    void fault_GetStatus(uint16_t *const allFaults);

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
     * CB_SetIndividualChannel function.
     *
     * @param enable    State of cell balancing. False (cell balancing disabled) or
     *                  True (cell balancing enabled).
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t CB_Enable(const bool enable);

    /*!
     * @brief This function enables or disables cell balancing for a specified channel
     * (0-13) and sets its timer.
     *
     * @param channelIndex Channel index. Use 0U for Channel 1, 1U for Channel 2, etc.
     * @param enable    True for enabling of CB, False otherwise.
     * @param timer     Timer for enabled CB driver in minutes. Value of zero
     *                  represents 30 seconds.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t CB_SetIndividualChannel(const uint8_t channelIndex, const bool enable,
                                         const uint16_t timer);

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
    bcc_status_t CB_SetIndividualCell(const uint8_t cellIndex, const bool enable,
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
    inline bcc_cid_t getCID() { return mCID; }

    /**
     * Returns true if current sensing is enabled. False if disabled
     */
    inline bool currentSenseEnabled() { return mCurrentSenseEnabled; }

    /**
     * Get the CAN state of the BCC. Also resets the state to OK.
     */
    can_state_t getCANstate();

    /**
     * Get the CAN state of global-level BCC functions. Also resets the state to OK.
     */
    static can_state_t getCANstateGlobal();

    /**
     * Check if the BCC has a valid configuration
     */
    bool hasValidConfig();

    /**
     * Returns true if balancing is on for the specified cell. False if off.
     * To check if any cell is balancing, pass -1 as the cellIndex (default)
     */
    bool isCellBalancing(const int8_t cellIndex = -1);

    /**
     * Returns true if balancing is on for the specified cell. False if off.
     * To check if any cell is balancing, pass -1 as the cellIndex (default)
     */
    bool isChannelBalancing(const int8_t channelIndex = -1);

    /**
     * Returns boolean vector indicating which cells are balancing
    */
    const std::vector<bool> getBalancingList(); 

    /**
     * Returns the number of connected cells
     */
    inline uint8_t getCellCount() { return mCellCount; }

    /**
     * Returns the number of connected NTCs
     */
    inline uint8_t getNTCCount() { return mNTCCount; }

    /**
     * Returns the raw measurements of the BCC
     */
    inline uint16_t const *getRawMeasurements() { return mRawMeasurements; }

    /**
     * Returns the time when the last measurement was received
     */
    inline uint32_t getTimeReceivedLastMeasurement() { return timeReceivedLastMeasurement; }

    /**
     * Returns the fault status registers of the BCC device.
     * Returned array has length BCC_STAT_CNT indexed by bcc_fault_status_t
     * @return uint16_t* Array of fault status registers.
     */
    inline uint16_t *getFaultStatusRegisters() { return mFaultStatusRegisters; }

    /**
     * Checks if this BCC is still present in the chain by reading it's CID
     */
    bool isPresent();
};
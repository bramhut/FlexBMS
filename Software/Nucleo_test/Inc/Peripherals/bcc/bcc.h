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

class BCC
{
private:

    /*******************************************************************************
     * DEVICE SPECIFIC SETTINGS
     ******************************************************************************/

    // Device type
    const bcc_device_t device;

    // Number of connected cells
    const uint16_t cellCount;

    // Map of connected cells
    const uint16_t cellMap;

    /*******************************************************************************
     * PRIVATE FUNCTIONS
     ******************************************************************************/

    /*!
     * @brief This function does two consecutive transitions of CSB_TX from low to
     * high.
     *
     * CSB_TX -> 0 for 25 us
     * CSB_TX -> 1 for 600 us
     * CSB_TX -> 0 for 25 us
     * CSB_TX -> 1 for 750*numberOfDevices us
     *
     */
    void BCC_WakeUpPatternTpl();

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
    bcc_status_t BCC_AssignCid(bcc_drv_config_t *const drvConfig,
                                      const bcc_cid_t cid);

    /*!
     * @brief This function wakes device(s) up, resets them (if needed), assigns
     * CIDs and checks the communication.
     *
     * @param drvConfig Pointer to driver instance configuration.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t BCC_InitDevices(bcc_drv_config_t *const drvConfig);

    /*!
     * @brief This function configures selected GPIO/AN pin as analog input, digital
     * input or digital output by writing the GPIO_CFG1[GPIOx_CFG] bit field.
     *
     * @param drvConfig Pointer to driver instance configuration.
     * @param cid       Cluster Identification Address of the BCC device.
     * @param gpioSel   Index of pin to be configured. Index starts at 0 (GPIO0).
     * @param mode      Pin mode. The only accepted enum items are:
     *                  BCC_PIN_ANALOG_IN_RATIO, BCC_PIN_ANALOG_IN_ABS,
     *                  BCC_PIN_DIGITAL_IN and BCC_PIN_DIGITAL_OUT.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t BCC_SetGpioCfg(bcc_drv_config_t *const drvConfig,
                                       const bcc_cid_t cid, const uint8_t gpioSel, const bcc_pin_mode_t mode);
                                       

    /*!
     * @brief This function check if all necesssary configuration data is set and valid.
     * For exmaple; Check if deviceCnt and cellCnt is set.
     * 
     * @author Arjan Blankestijn
     * 
     * @return true if configuration is valid, false otherwise
    */
    bool checkConfiguration();

public:


    BCC(bcc_device_t device, uint16_t cellCount);


    /*!
     * @brief This function initializes the battery cell controller device(s),
     * assigns CID and initializes internal driver data.
     *
     * Note that this function initializes only the INIT register of connected
     * BCC device(s).
     *
     * Note that the function is implemented for an universal use. It is capable to
     * move BCC devices into the NORMAL mode from all modes (INIT, IDLE, SLEEP and
     * NORMAL) except the RESET mode. Such implementation can generate more wake-up
     * sequences via CSB_TX pin than required for the current mode. These extra
     * sequences project to the COM_ERR_COUNT bit field in COM_STATUS register.
     * Therefore, it is recommended to clear the COM_STATUS register after calling
     * of this function.
     *
     * 
     * @return bcc_status_t Error code.
     */
    bcc_status_t BCC_Init();

    /*!
     * @brief This function sets sleep mode to all battery cell controller devices.
     *
     * In case of TPL communication mode, MC33664 has to be put into the sleep mode
     * separately, by the BCC_TPL_Disable function.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t BCC_Sleep();


    /*!
     * @brief This function resets BCC device using software reset. It enters reset
     * via SPI or TPL interface. In TPL, you can use BCC_CID_UNASSIG as CID
     * parameter for software reset of all devices in the chain (global write).
     *
     * @param drvConfig Pointer to driver instance configuration.
     * @param cid       Cluster Identification Address of the BCC device.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t BCC_SoftwareReset(bcc_drv_config_t *const drvConfig,
                                   const bcc_cid_t cid);

    /*!
     * @brief This function resets BCC device using RESET pin.
     *
     * @param drvConfig Pointer to driver instance configuration.
     */
    void BCC_HardwareReset(const bcc_drv_config_t *const drvConfig);

    /*!
     * @brief This function enables MC33664 device (sets a normal mode).
     * Intended for TPL mode only!
     *
     * During the driver initialization (BCC_Init function), normal mode of MC33664
     * is set automatically. This function can be used when sleep mode of BCC
     * device(s) and MC33664 together is required. The typical function flow is as
     * follows: BCC_Sleep -> BCC_TPL_Disable -> ... -> BCC_TPL_Enable -> BCC_WakeUp.
     *
     * @param drvInstance BCC driver instance (content of the bcc_drv_config_t
     *                    structure). Passed to the external functions defined by
     *                    the user in order to distinguish between more TPL
     *                    interfaces.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t BCC_TPL_Enable(const uint8_t drvInstance);

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
    void BCC_TPL_Disable(const uint8_t drvInstance);

    
    /*!
     * @brief This function starts ADC conversion in selected BCC device. It sets
     * number of samples to be averaged and Start of Conversion bit in ADC_CFG
     * register.
     *
     * You can use function BCC_Meas_IsConverting to check the conversion status or
     * a blocking function BCC_Meas_StartAndWait instead.
     * Measured values can be retrieved e.g. by function BCC_Meas_GetRawValues.
     *
     * @param drvConfig Pointer to driver instance configuration.
     * @param cid       Cluster Identification Address of the BCC device.
     * @param avg       Number of samples to be averaged.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t BCC_Meas_StartConversion(bcc_drv_config_t *const drvConfig,
                                          const bcc_cid_t cid, const bcc_avg_t avg);

    /*!
     * @brief This function starts ADC conversion for all devices in TPL chain. It
     * uses a Global Write command to set ADC_CFG register. Intended for TPL mode
     * only!
     *
     * You can use function BCC_Meas_IsConverting to check conversion status and
     * function BCC_Meas_GetRawValues to retrieve the measured values.
     *
     * @param drvConfig   Pointer to driver instance configuration.
     * @param adcCfgValue Value of ADC_CFG register to be written to all devices in
     *                    the chain. Note that SOC bit is automatically added by
     *                    this function.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t BCC_Meas_StartConversionGlobal(bcc_drv_config_t *const drvConfig,
                                                uint16_t adcCfgValue);

    /*!
     * @brief This function checks status of conversion defined by End of Conversion
     * bit in ADC_CFG register.
     *
     * @param drvConfig Pointer to driver instance configuration.
     * @param cid       Cluster Identification Address of the BCC device.
     * @param completed Pointer to check result. True if a conversion is complete.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t BCC_Meas_IsConverting(bcc_drv_config_t *const drvConfig,
                                       const bcc_cid_t cid, bool *const completed);

    /*!
     * @brief This function starts an on-demand conversion in selected BCC device
     * and waits for completion.
     *
     * @param drvConfig Pointer to driver instance configuration.
     * @param cid       Cluster Identification Address of the BCC device.
     * @param avg       Number of samples to be averaged.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t BCC_Meas_StartAndWait(bcc_drv_config_t *const drvConfig,
                                       const bcc_cid_t cid, const bcc_avg_t avg);

    /*!
     * @brief This function reads the measurement registers and returns raw values.
     *
     * Macros defined in bcc_utils.h can be used to perform correct unit conversion.
     *
     * Warning: The "data ready" flag is ignored.
     *
     * @param drvConfig    Pointer to driver instance configuration.
     * @param cid          Cluster Identification Address of the BCC device.
     * @param measurements Array containing all values measured by BCC. Size of this
     *                     array must be BCC_MEAS_CNT*16b. Indexes into the array
     *                     are defined in enumeration bcc_measurements_t
     *                     placed in this header file.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t BCC_Meas_GetRawValues(bcc_drv_config_t *const drvConfig,
                                       const bcc_cid_t cid, uint16_t *const measurements);

    /*!
     * @brief This function reads the Coulomb counter registers.
     *
     * Note that the Coulomb counter is independent on the on-demand conversions.
     *
     * @param drvConfig Pointer to driver instance configuration.
     * @param cid       Cluster Identification Address of the BCC device.
     * @param cc        Coulomb counter data.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t BCC_Meas_GetCoulombCounter(bcc_drv_config_t *const drvConfig,
                                            const bcc_cid_t cid, bcc_cc_data_t *const cc);

    /*!
     * @brief This function reads the ISENSE measurement and converts it to [uV].
     *
     * @param drvConfig  Pointer to driver instance configuration.
     * @param cid        Cluster Identification Address of the BCC device.
     * @param isenseVolt Pointer to memory where the ISENSE voltage (in [uV]) will
     *                   be stored.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t BCC_Meas_GetIsenseVoltage(bcc_drv_config_t *const drvConfig,
                                           const bcc_cid_t cid, int32_t *const isenseVolt);

    /*!
     * @brief This function reads the stack measurement and converts it to [uV].
     *
     * @param drvConfig Pointer to driver instance configuration.
     * @param cid       Cluster Identification Address of the BCC device.
     * @param stackVolt Pointer to memory where the stack voltage (in [uV]) will
     *                  be stored.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t BCC_Meas_GetStackVoltage(bcc_drv_config_t *const drvConfig,
                                          const bcc_cid_t cid, uint32_t *const stackVolt);

    /*!
     * @brief This function reads the cell measurements and converts them to [uV].
     *
     * @param drvConfig Pointer to driver instance configuration.
     * @param cid       Cluster Identification Address of the BCC device.
     * @param cellVolt  Pointer to the array where the cell voltages (in [uV]) will
     *                  be stored. Array must have an suitable size (14*32b for
     *                  MC33771C and 6*32b for MC33772C). Cell 1 voltage is stored
     *                  at [0], Cell 2 voltage at [1], etc.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t BCC_Meas_GetCellVoltages(bcc_drv_config_t *const drvConfig,
                                          const bcc_cid_t cid, uint32_t *const cellVolt);

    /*!
     * @brief This function reads the voltage measurement of a selected cell and
     * converts it to [uV].
     *
     * @param drvConfig Pointer to driver instance configuration.
     * @param cid       Cluster Identification Address of the BCC device.
     * @param cellIndex Cell index. Use 0U for CELL 1, 1U for CELL2, etc.
     * @param cellVolt  Pointer to memory where the cell voltage (in [uV]) will
     *                  be stored.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t BCC_Meas_GetCellVoltage(bcc_drv_config_t *const drvConfig,
                                         const bcc_cid_t cid, uint8_t cellIndex, uint32_t *const cellVolt);

    /*!
     * @brief This function reads the voltage measurement for all ANx and converts
     * them to [uV]. Intended for ANx configured for absolute measurements only!
     *
     * @param drvConfig Pointer to driver instance configuration.
     * @param cid       Cluster Identification Address of the BCC device.
     * @param anVolt    Pointer to the array where ANx voltages (in [uV]) will
     *                  be stored. Array must have an suitable size
     *                  (BCC_GPIO_INPUT_CNT * 32b). AN0 voltage is stored at [0],
     *                  AN1 at [1], etc.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t BCC_Meas_GetAnVoltages(bcc_drv_config_t *const drvConfig,
                                        const bcc_cid_t cid, uint32_t *const anVolt);

    /*!
     * @brief This function reads the voltage measurement of a selected ANx and
     * converts it to [uV]. Intended for ANx configured for absolute measurements
     * only!
     *
     * @param drvConfig Pointer to driver instance configuration.
     * @param cid       Cluster Identification Address of the BCC device.
     * @param anIndex   ANx index. Use 0U for AN0, 1U for AN1, etc.
     * @param anVolt    Pointer to memory where the ANx voltage (in [uV]) will
     *                  be stored.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t BCC_Meas_GetAnVoltage(bcc_drv_config_t *const drvConfig,
                                       const bcc_cid_t cid, uint8_t anIndex, uint32_t *const anVolt);

    /*!
     * @brief This function reads the BCC temperature and converts it to the
     * selected unit.
     *
     * @param drvConfig Pointer to driver instance configuration.
     * @param cid       Cluster Identification Address of the BCC device.
     * @param unit      Temperature unit.
     * @param icTemp    Pointer to memory where the IC temperature will be stored.
     *                  Resolution is 0.1; unit is selected by parameter "unit".
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t BCC_Meas_GetIcTemperature(bcc_drv_config_t *const drvConfig,
                                           const bcc_cid_t cid, bcc_temp_unit_t unit, int16_t *const icTemp);

    /*!
     * @brief This function reads the fault status registers of the BCC device.
     *
     * @param drvConfig Pointer to driver instance configuration.
     * @param cid       Cluster Identification Address of the BCC device.
     * @param fltStatus Array containing all fault status information provided by
     *                  BCC. Required size of the array is BCC_STAT_CNT*16b.
     *                  Indexes into the array are defined in bcc_fault_status_t
     *                  enumeration placed in this header file.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t BCC_Fault_GetStatus(bcc_drv_config_t *const drvConfig,
                                     const bcc_cid_t cid, uint16_t *const fltStatus);

    /*!
     * @brief This function clears selected fault status register.
     *
     * @param drvConfig Pointer to driver instance configuration.
     * @param cid       Cluster Identification Address of the BCC device.
     * @param statSel   Selection of a fault status register to be cleared. See
     *                  definition of this enumeration in this header file.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t BCC_Fault_ClearStatus(bcc_drv_config_t *const drvConfig,
                                       const bcc_cid_t cid, const bcc_fault_status_t statSel);

    /*!
     * @brief This function sets the mode of one BCC GPIOx/ANx pin.
     *
     * @param drvConfig Pointer to driver instance configuration.
     * @param cid       Cluster Identification Address of the BCC device.
     * @param gpioSel   Index of pin to be configured. Index starts at 0 (GPIO0).
     * @param mode      Pin mode.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t BCC_GPIO_SetMode(bcc_drv_config_t *const drvConfig,
                                  const bcc_cid_t cid, const uint8_t gpioSel, const bcc_pin_mode_t mode);

    /*!
     * @brief This function reads a value of one BCC GPIO pin.
     *
     * Note that such GPIO/AN pin should be configured as digital input or output.
     *
     * @param drvConfig Pointer to driver instance configuration.
     * @param cid       Cluster Identification Address of the BCC device.
     * @param gpioSel   Index of GPIOx to be read. Index starts at 0 (GPIO0).
     * @param val       Pointer where the pin value will be stored. Possible values
     *                  are: False (logic 0, low level) and True (logic 1, high
     *                  level).
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t BCC_GPIO_ReadPin(bcc_drv_config_t *const drvConfig,
                                  const bcc_cid_t cid, const uint8_t gpioSel, bool *const val);

    /*!
     * @brief This function sets output value of one BCC GPIO pin.
     *
     * Note that this function is determined for GPIO/AN pins configured as output
     * pins.
     *
     * @param drvConfig Pointer to driver instance configuration.
     * @param cid       Cluster Identification Address of the BCC device.
     * @param gpioSel   Index of GPIO output to be set. Index starts at 0 (GPIO0).
     * @param val       Output value. Possible values are: False (logic 0, low
     *                  level) and True (logic 1, high level).
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t BCC_GPIO_SetOutput(bcc_drv_config_t *const drvConfig,
                                    const bcc_cid_t cid, const uint8_t gpioSel, const bool val);

    /*!
     * @brief This function enables or disables the cell balancing via
     * SYS_CFG1[CB_DRVEN] bit.
     *
     * Note that each cell balancing driver needs to be setup separately, e.g. by
     * BCC_CB_SetIndividual function.
     *
     * @param drvConfig Pointer to driver instance configuration.
     * @param cid       Cluster Identification Address of the BCC device.
     * @param enable    State of cell balancing. False (cell balancing disabled) or
     *                  True (cell balancing enabled).
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t BCC_CB_Enable(bcc_drv_config_t *const drvConfig,
                               const bcc_cid_t cid, const bool enable);

    /*!
     * @brief This function enables or disables cell balancing for a specified cell
     * and sets its timer.
     *
     * @param drvConfig Pointer to driver instance configuration.
     * @param cid       Cluster Identification Address of the BCC device.
     * @param cellIndex Cell index. Use 0U for CELL 1, 1U for CELL2, etc.
     * @param enable    True for enabling of CB, False otherwise.
     * @param timer     Timer for enabled CB driver in minutes. Value of zero
     *                  represents 30 seconds.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t BCC_CB_SetIndividual(bcc_drv_config_t *const drvConfig,
                                      const bcc_cid_t cid, const uint8_t cellIndex, const bool enable,
                                      const uint16_t timer);

    /*!
     * @brief This function pauses cell balancing. It can be useful during an on
     * demand conversion. As a result more precise measurement can be done. Note
     * that it is user obligation to re-enable cell balancing after measurement
     * ends.
     *
     * @param drvConfig Pointer to driver instance configuration.
     * @param cid       Cluster Identification Address of the BCC device.
     * @param pause     True (pause) or False (unpause).
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t BCC_CB_Pause(bcc_drv_config_t *const drvConfig,
                              const bcc_cid_t cid, const bool pause);

    /*!
     * @brief This function reads a fuse mirror register of selected BCC device.
     *
     * @param drvConfig Pointer to driver instance configuration.
     * @param cid       Cluster Identification Address of the BCC device.
     * @param fuseAddr  Address of a fuse mirror register to be read.
     * @param value     Pointer to memory where the read value will be stored.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t BCC_FuseMirror_Read(bcc_drv_config_t *const drvConfig,
                                     const bcc_cid_t cid, const uint8_t fuseAddr, uint16_t *const value);

    /*!
     * @brief This function writes a fuse mirror register of selected BCC device.
     *
     * @param drvConfig Pointer to driver instance configuration.
     * @param cid       Cluster Identification Address of the BCC device.
     * @param fuseAddr  Address of a fuse mirror register to be written.
     * @param value     Value to be written.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t BCC_FuseMirror_Write(bcc_drv_config_t *const drvConfig,
                                      const bcc_cid_t cid, const uint8_t fuseAddr, const uint16_t value);

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
     * @param drvConfig Pointer to driver instance configuration.
     * @param cid       Cluster Identification Address of the BCC device.
     * @param guid      Pointer to memory where 37b unique ID will be stored.
     *
     * @return bcc_status_t Error code.
     */
    bcc_status_t BCC_GUID_Read(bcc_drv_config_t *const drvConfig,
                               const bcc_cid_t cid, uint64_t *const guid);


    
};
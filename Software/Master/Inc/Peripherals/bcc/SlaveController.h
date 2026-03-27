#pragma once
#include "bcc/bcc.h"
#include "CAN.h"
#include <vector>

/*******************************************************************************
 * Definitions
 ******************************************************************************/

namespace SlaveController
{
    struct RegisterRequest
    {
        uint8_t cid;
        uint8_t regAddr;
    };

    struct RegisterReponse
    {
        uint16_t regValue;
        bcc_status_t status;
    };

    enum BMSFault
    {
        INVALID_CONFIG,
        TPL_FAULT,
        CID_INITIALIZATION_FAULT,
        REGISTER_INITIALIZATION_FAULT,
        CELL_BALANCING_FAULT,
        DIAGNOSTICS_FAULT,
        OVERVOLTAGE_LIMIT,
        UNDERVOLTAGE_LIMIT,
        TEMPERATURE_LIMIT,
        OVERCURRENT_LIMIT,
        IC_TEMPERATURE,
        SOC_LIMIT,
        OPEN_SHORT_FAULT, // A pin (can be CB or GPIO) is detected short or open
        SYSTEM_FAULT,
        COMMUNICATION_TIMEOUT,
    };

    enum BMSState
    {
        DEVICE_INITIALIZATION,
        REGISTER_INITIALIZATION,
        PERFORMING_DIAGNOSTICS,
        RUNNING,
        PANIC
    };

    /*******************************************************************************
     * API
     ******************************************************************************/

    /*!
     * @brief Setup function only to be called once
     *
     * @param Pointer to a CAN class instance. Make sure that the CAN class is already initialized
     */
    void setup(CAN *can);

    /*!
     * @brief Check if there is new (valid) data available
     *
     * @return new data available flag
     */
    bool isNewDataAvailable();

    /*!
     * @brief Get the current faults
     *
     * @return Current faults
     */
    uint16_t getFaults();

    /*!
     * @brief Clear the current faults if allowed
     */
    void clearFaults();

    /*!
     * @brief Get the current state of the BMS
     *
     * @return Current state
     */
    BMSState getState();

    /*!
     * @brief Get the number of slaves
     *
     * @return Number of slaves
     */
    size_t getNumOfSlaves();

    /*!
     * @brief Get the cell count for the entire pack
     *
     * @return Number of cells in the pack
     */
    size_t getCellCount();

    /*!
     * @brief Get the cell count per slave
     *
     * @return Vector of cell count per slave
     */
    std::vector<size_t> getCellCountPerSlave();

    /*!
     * @brief Get the NTC count per slave
     *
     * @return Vector of NTC count per slave
     */
    std::vector<size_t> getNTCCountPerSlave();

    /*!
    * @brief Get the pack voltage in [uV]
    *
    * @return uint32_t pack voltage [uV]
    */    
    uint32_t getPackVoltage();

    // /*!
    //  * @brief Get the current in [1/64 A]
    //  *
    //  * @return int16_t current [1/64 A]
    //  */
    // int16_t getCurrent();

    /*!
    * @brief Check whether charging is currently allowed
    *
    * @return true if charging is allowed
    */    
    bool isChargingAllowed();

    /*!
     * @brief Get the cell voltages per slave [uV]
     *
     * @return 2D vector of cell voltages (uint32_t) per slave [uV]
     */
    const std::vector<std::vector<uint32_t>>& getCellVoltages();

        /*!
     * @brief Get a list of cells that are balancing per slave
     *
     * @return 2D vector of cell balance states per slave [uV]
     */
    const std::vector<std::vector<bool>> getBalancingList();

    /*!
    * @brief Get a list of lists of which cells are balancing
    * @return 2D vector of which cells are balancing
    */
    // const std::vector<std::vector<uint32_t>>& getBalanceList();

    /*!
     * @brief Get the minimum cell voltage [uV]
     *
     * @return uint32_t minimum cell voltage [uV]
     */
    uint32_t getMinCellVoltage();

    /*!
     * @brief Get the maximum cell voltage [uV]
     *
     * @return uint32_t maximum cell voltage [uV]
     */
    uint32_t getMaxCellVoltage();

    /*!
     * @brief Get the NTC temperatures per slave [raw]
     *
     * @return 2D vector of NTC temperatures (uint16_t) per slave [raw]
     */
    const std::vector<std::vector<uint16_t>>& getNTCtemps();

    /*!
     * @brief Get the minimum NTC temperature [raw]
     *
     * @return uint16_t minimum NTC temperature [raw]
     */
    uint16_t getMinNTCtemp();

    /*!
     * @brief Get the maximum NTC temperature [raw]
     *
     * @return uint16_t maximum NTC temperature [raw]
     */
    uint16_t getMaxNTCtemp();

    /*!
     * @brief Get a vector of IC temperatures [raw]
     *
     * @return Vector of IC temperatures (uint16_t) [raw]
     */
    const std::vector<uint16_t>& getICtemps();

    /*!
     * @brief Get the minimum IC temperature [raw]
     *
     * @return uint16_t minimum IC temperature [raw]
     */
    uint16_t getMinICtemp();

    /*!
     * @brief Get the maximum IC temperature [raw]
     *
     * @return uint16_t maximum IC temperature [raw]
     */
    uint16_t getMaxICtemp();

    /*!
     * @brief Get the State of Charge [raw]
     *
     * @return uint16_t State of Charge [raw]
     */
    uint16_t getSoC();

    /*!
     * @brief Set the State of Charge [raw]
     *
     * @param uint16_t State of Charge [raw]
     */
    void setSoC(uint16_t soc);

    void requestRegister(RegisterRequest requestInfo, bool *flag, RegisterReponse *regValue);
}
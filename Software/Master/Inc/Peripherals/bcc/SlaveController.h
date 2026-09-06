#pragma once
#include "bcc/bcc.h"
#include "CAN.h"
#include "RuntimeConfiguration.h"
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

    struct DiagnosticReport
    {
        uint8_t cid{};
        uint16_t failedChecks{};
        uint8_t status{};
        uint8_t failedDiagnostic{0xFFU};
    };

    /*! @brief Coherent battery data and directional permissions for inverter CAN. */
    struct BatteryCanSnapshot
    {
        bool valid{};
        bool measurementsFresh{};
        bool commonSafe{};
        bool chargeAllowed{};
        bool dischargeAllowed{};
        bool socValid{};
        bool currentSensingEnabled{};

        bool cellOverVoltage{};
        bool cellUnderVoltage{};
        bool overTemperature{};
        bool underTemperature{};
        bool overCurrent{};
        bool communicationFault{};
        bool internalFault{};

        uint32_t packVoltageUv{};
        double packCurrentA{};
        double chargeVoltageV{};
        double dischargeVoltageV{};
        double chargeCurrentA{};
        double dischargeCurrentA{};
        double averageTemperatureC{};
        uint16_t socPercent{};
        size_t cellCount{};
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
        OPEN_SHORT_FAULT, // A pin (can be CB or GPIO) is detected short or open
        SYSTEM_FAULT,
        COMMUNICATION_TIMEOUT,
        NO_CONFIG,
    };

    enum BMSState
    {
        DEVICE_INITIALIZATION,
        REGISTER_INITIALIZATION,
        PERFORMING_DIAGNOSTICS,
        RUNNING,
        CRITICAL
    };

    /*! @brief Coherent copy of one complete BCC measurement publication. */
    struct MeasurementSnapshot
    {
        bool valid{};
        bool measurementsFresh{};
        bool socValid{};
        bool currentSensingEnabled{};
        uint32_t sequence{};
        uint32_t packVoltageUv{};
        double packCurrentA{};
        uint16_t socRaw{};
        uint32_t minCellVoltageUv{};
        uint32_t maxCellVoltageUv{};
        uint16_t minNtcTemperatureRaw{};
        uint16_t maxNtcTemperatureRaw{};
        uint16_t minIcTemperatureRaw{};
        uint16_t maxIcTemperatureRaw{};
        std::vector<std::vector<uint32_t>> cellVoltages{};
        std::vector<std::vector<uint16_t>> ntcTemperatures{};
        std::vector<uint16_t> icTemperatures{};
        std::vector<uint16_t> balancingMasks{};
    };

    /*! @brief Persistent directional energy counters in micro-watt-hours. */
    struct EnergySnapshot
    {
        bool valid{};
        uint64_t chargedEnergyUWh{};
        uint64_t dischargedEnergyUWh{};
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
    bool isNewDataAvailable(uint32_t &lastSeenMeasurement);

    /*! @brief True when the most recent complete measurement set is still valid. */
    bool areMeasurementsFresh();

    /*! @brief Return the sequence number of the latest committed measurement. */
    uint32_t getMeasurementSequence();

    /*! @brief Return one coherent copy of the latest complete measurement set. */
    MeasurementSnapshot getMeasurementSnapshot();

    /*! @brief Return the latest persistent charge/discharge energy counters. */
    EnergySnapshot getEnergySnapshot();

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

    /*! @brief Get the most recently measured pack current in [A]. */
    double getCurrent();

    /*! @brief True when the BMS permits the HV supervisor to operate. */
    bool isHVReady();

    /*!
    * @brief Check whether charging is currently allowed
    *
    * @return true if charging is allowed
    */    
    bool isChargingAllowed();

    /*! @brief Check whether discharging is currently allowed. */
    bool isDischargingAllowed();

    /*! @brief Return a coherent snapshot for inverter CAN publication. */
    BatteryCanSnapshot getBatteryCanSnapshot();

    // Automatic balancing is enabled by default. This setting is an additional
    // gate and never overrides normal balancing safety conditions.
    void setBalancingEnabled(bool enabled);
    bool isBalancingEnabled();

    /*! @brief Return the most recent startup diagnostic result for one slave. */
    bool getDiagnosticReport(uint8_t slaveIndex, DiagnosticReport &report);

    /*!
     * @brief Get the cell voltages per slave [uV]
     *
     * @return 2D vector of cell voltages (uint32_t) per slave [uV]
     */
    std::vector<std::vector<uint32_t>> getCellVoltages();

        /*!
     * @brief Get a list of cells that are balancing per slave
     *
     * @return 2D vector of cell balance states per slave [uV]
     */
    const std::vector<std::vector<bool>> getBalancingList();

    /*! @brief Get the twelve-cell UART balance bitmap for one zero-based slave index. */
    uint16_t getBalancingMask(size_t slaveIndex);

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
    std::vector<std::vector<uint16_t>> getNTCtemps();

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
    std::vector<uint16_t> getICtemps();

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

    /*! @brief True only when current sensing is configured and retained SOC is valid. */
    bool isSoCValid();

    /*! @brief True when a configured BCC supplies pack-current measurements. */
    bool isCurrentSensingEnabled();

    /*! @brief Validate a complete runtime configuration before persisting it. */
    bool validateRuntimeConfiguration(const RuntimeConfiguration::Values &values);

    /*! @brief Return the UTC instant of the last automatic full-SOC calibration. */
    bool getLastSoCCalibrationUnixTime(uint32_t &unixTime);

    /*!
     * @brief Set the State of Charge [raw]
     *
     * @param uint16_t State of Charge [raw]
     */
    void setSoC(uint16_t soc);

    bool requestRegister(RegisterRequest requestInfo);
    bool takeRegisterResponse(RegisterReponse &response);
}

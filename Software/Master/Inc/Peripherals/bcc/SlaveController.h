#pragma once
#include "bcc/bcc.h"
#include "bcc/MeasurementFrame.h"
#include "CAN.h"
#include "RuntimeConfiguration.h"
#include <cstddef>

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

    /*! @brief Copy a coherent frame if its sequence differs from lastSeenMeasurement.
     *
     * Waits at most two milliseconds for publication. On failure, destination
     * and lastSeenMeasurement remain unchanged so the caller can retry.
     */
    bool tryGetNewMeasurementFrame(uint32_t &lastSeenMeasurement, MeasurementFrame &destination);

    /*! @brief True when the most recent complete measurement set is still valid. */
    bool areMeasurementsFresh();

    /*! @brief Return the sequence number of the latest committed measurement. */
    uint32_t getMeasurementSequence();

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
    * @brief Get the pack voltage in [uV]
    *
    * @return uint32_t pack voltage [uV]
    */    
    uint32_t getPackVoltage();

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

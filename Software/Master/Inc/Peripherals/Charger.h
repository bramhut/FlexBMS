#include "CAN.h"

namespace Charger {
    void setup(CAN *can);
    void loop();

    /**
     * @brief Turn charging on or off.
     * @param enable True to enable charging, false to disable. 
     * @param maxCurrent Maximum charging current in Amperes.
     */
    bool toggleCharging(bool enable, double maxCurrent = 1);

    /**
     * @brief Check if the charger is connected.
     * @return True if the charger is connected, false otherwise.
     */
    bool isConnected();

    /**
     * @brief Get the output voltage of the charger.
     * @return Output voltage in Volts.
     */
    double getOutputVoltage();

    /**
     * @brief Get the output current of the charger.
     * @return Output current in Amperes.
     */
    double getOutputCurrent();

    /**
     * @brief Get the maximum charging current allowed that is currently set.
     * @return Maximum charging current in Amperes.
     */
    double getMaxChargingCurrent();

    /**
     * @brief Check if charging is enabled. (AKA if charger is charging)
     * @return True if charging is enabled, false otherwise.
     */
    bool isChargingEnabled();

    



}
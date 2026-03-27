#include "CAN.h"
#include "TimeFunctions.h"
#include "Debug.h"
#include "bcc/UserSettings.h"
#include "bcc/SlaveController.h"

#define DEBUG_LVL 2 // Set the debug level for this file

namespace Charger
{

    namespace
    {
        CAN *chargerCAN = nullptr;                // Pointer to the CAN instance used for charger communication
        const uint32_t receiveCANId = 0x18FF50E5; // Charger ID, used for the charger messages
        const uint32_t sendCANId = 0x1806E5F4;    // Charger ID for sending messages

        osMessageQueueId_t chargerQueue = nullptr; // Message queue for charger messages
        uint32_t lastChargerMessage = 0;           // Last time a charger message was received [ms]

        double outputVoltage = 0; // Output voltage of the charger
        double outputCurrent = 0; // Output current of the charger

        double maxChargingCurrent = 0; // Maximum charging current allowed, in [A]. This is the current that the charger will use to charge the battery.

        uint8_t chargerStatus = 0; // Status of the charger, see CAN datasheet for details of the status byte

        bool chargingEnabled = false; // Flag to indicate if charging is enabled

        uint32_t lastLoop = 0; // Last time the loop was executed [ms]

        /*!
         * @brief Get the currently allowed maximum charging voltage in [V]. Be sure to implement your own margin to avoid overvoltage faults.
         *
         * @return double maximum charging voltage [uV]
         */
        double calcMaxChargingVoltage()
        {
            // This algorithm calculates the maximum charging voltage based on the highest cell voltage and the overvoltage limit
            // It assumes that the maximum charging voltage is equal to the difference between the highest cell voltage and the overvoltage limit, applied to all cells.
            // This might overestimate the voltage at the start, however it will converge to the correct value as the cells are getting charged.

            int32_t highestVoltageMargin = DEFAULT_SETTINGS.SAFETY_LIMITS.OVERVOLTAGE_LIMIT * 1e6 - SlaveController::getMaxCellVoltage() * 1e6; // Calculate the highest voltage margin in uV
            return ((double)(SlaveController::getPackVoltage() + highestVoltageMargin * SlaveController::getCellCount())) / 1000000;
        }

        /**
         * @brief Parse the charger message received from the CAN bus.
         * @param frame Pointer to the CAN frame containing the charger message.
         */
        void parseChargerMessage(CAN::Frame *frame)
        {
            if (frame->id == receiveCANId)
            {
                // Parse the charger message to get the output voltage and current
                outputVoltage = ((double)((uint16_t)frame->data[0] << 8 | frame->data[1])) / 10; // Voltage in V
                outputCurrent = ((double)((uint16_t)frame->data[2] << 8 | frame->data[3])) / 10; // Current in A
                chargerStatus = frame->data[2];                                                  // Status byte
            }
            else
            {
                PRINTF_ERR("[Charger] Received message with unknown ID: %08X\n", frame->id);
            }
        }

        /**
         * @brief Send a charger message to the CAN bus.
         */
        void sendChargerMessage(CAN *can)
        {
            CAN::Frame frame;
            frame.id = sendCANId;    // Set the ID for the charger message
            frame.isExtended = true; // Use standard ID
            frame.isRtr = false;     // Not a remote frame
            frame.length = 8;        // Length of the data

            // Set the data for the charger message
            uint16_t outVolt = (uint16_t)(calcMaxChargingVoltage() * 10); // Voltage in mV
            uint16_t maxCurrent = (uint16_t)(outputCurrent * 10);         // Current in mA

            frame.data16[0] = (outVolt >> 8) | (outVolt << 8); // Voltage in mV

            frame.data16[1] = (maxCurrent >> 8) | (maxCurrent << 8); // Current in mA

            if (chargingEnabled)
            {
                frame.data[4] = 0x00; // Enable charging
            }
            else
            {
                frame.data[4] = 0x01; // Disable charging
            }

            // Temporarily commented out because CAN is broken
            // if (!can->sendMessage(frame))
            // {
            //     PRINTF_ERR("[Charger] Failed to send charger message\n");
            // }
        }

    }

    void setup(CAN *can)
    {
        chargerCAN = can; // Set the CAN instance for charger communication
        // Initialize the charger, set up CAN communication, etc.
        chargerQueue = osMessageQueueNew(1, sizeof(CAN::Frame), NULL); // Create a new message queue for charger messages
        chargerCAN->addListener(receiveCANId, chargerQueue);           // Add listener for charger messages
    }

    bool isConnected()
    {
        // Check if the charger is connected by checking the last message time
        return ((millis() - lastChargerMessage < 1000) && lastChargerMessage != 0); // Consider it connected if a message was received in the last 1 seconds
    }

    double getMaxChargingCurrent()
    {
        // Return the maximum charging current allowed
        return maxChargingCurrent;
    }

    void loop()
    {

        // Check if CAN messages are available
        CAN::Frame frame; // Frame to hold the received CAN message
        if (osMessageQueueGet(chargerQueue, &frame, NULL, 0) == osOK)
        {
            parseChargerMessage(&frame);   // Parse the DCDC message to get the battery voltage
            lastChargerMessage = millis(); // Update the last DCDC message time
        }

        if (SlaveController::isChargingAllowed() == false || !isConnected())
        {
            chargingEnabled = false; // If charging is not allowed, disable charging
        }

        if (millis() - lastLoop > DEFAULT_SETTINGS.CAN_CHARGING_MSG_PERIOD) // Run the loop every second
        {
            lastLoop = millis(); // Update the last loop time
            // sendChargerMessage(chargerCAN);
        }
    }

    bool isChargingEnabled()
    {
        return chargingEnabled; // Return the charging enabled flag
    }

    double getOutputVoltage()
    {
        return outputVoltage; // Return the output voltage of the charger
    }

    double getOutputCurrent()
    {
        return outputCurrent; // Return the output current of the charger
    }

    bool toggleCharging(bool enable, double maxCurrent)
    {
        if (SlaveController::isChargingAllowed() && isConnected())
        { // Check if charging is allowed and the charger is connected
            if (maxCurrent < 0 || maxCurrent > DEFAULT_SETTINGS.SAFETY_LIMITS.CHARGE_CURRENT_LIMIT)
            {
                PRINTF_ERR("[Charger] Invalid maximum charging current: %.2f A. Must be between 0 and %.2f A\n", maxCurrent, DEFAULT_SETTINGS.SAFETY_LIMITS.CHARGE_CURRENT_LIMIT);
                return false; // Invalid maximum current
            }
            else
            {
                maxChargingCurrent = maxCurrent; // Set the maximum charging current
                chargingEnabled = enable;        // Set the charging enabled flag
                return true;
            }
        }
        else
        {
            PRINTF_ERR("[Charger] Charging is not allowed in the current state\n");
            return false; // Charging is not allowed
        }
    }

}
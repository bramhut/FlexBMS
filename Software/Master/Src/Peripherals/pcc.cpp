#include "pcc.h"
#include "BoardIO.h"
#include "Debug.h"
#include "Charger.h"
#include "bcc/UserSettings.h"

#define DEBUG_LVL 2 // Set the debug level for this file

#define DBC_BIT_POS(x) (x + 7 - 2 * (x % 8)) // Shamelessly stolen from the FCCU code.

namespace PCC
{
  namespace
  {
    // Constants
    const uint32_t PRECHARGE_TIMEOUT = 10000; // Precharge timeout in milliseconds
    const double PRECHARGE_PERCENTAGE = 0.95; // Percentage of the battery voltage to reach before enabling PCC
    const uint32_t CAN_TIMEOUT_TIME = 1000;   // Timeout time for CAN messages [ms]

    // CAN stuff
    const uint32_t dcdcID = 0x525;
    const uint32_t compID = 0x526; 
    CAN *mCan = nullptr;           // Pointer to the CAN instance used for PCC communication

    uint32_t lastDCDCMessage = 0;       // Last time a DCDC message was received [ms]
    uint32_t lastCompressorMessage = 0; // Last time a compressor message was received [ms]

    osMessageQueueId_t dcdcQueue = nullptr;       // Message queue for DCDC messages
    osMessageQueueId_t compressorQueue = nullptr; // Message queue for compressor messages

    // Measured values
    double dcdcVoltage = 0;       // Voltage of the DCDC
    double compressorVoltage = 0; // Voltage of the compressor
    double tsVoltage = 0;         // Estimated TS voltage based on dcdc/compressor or charger voltage

    PCC_STATE pccState = PCC_STATE::STARTUP;  // Current state of the Power Control Circuit (PCC)
    PCC_ERROR pccError = PCC_ERROR::NO_ERROR; // Current error state of the PCC
    uint32_t prechargeStartTime = 0;
    TSAC_LOCATION tsacLocation = TSAC_LOCATION::UNKOWN; // Location of the TSAC, used to determine if the PCC is in the car or in the charging cart

    uint32_t lastDebugInfo = 0; // Last time debug info was printed [ms]

    bool receivedFinishPrechargeCommand = false; // Flag to indicate if the finish precharge command was received

    uint32_t lastPrechargeTime = 0;

    void printDebugInfo()
    {
      if (millis() - lastDebugInfo > 1000) // Print debug info every second
      {
        PRINTF_ERR("[PCC] DCDC voltage: %.2f V\n, Compressor voltage: %.2f V\n, Charger voltage: %.2f V\n, TSAC location: %d\n, PCC state: %d\n, PCC error: %d\n",
                   dcdcVoltage, compressorVoltage, Charger::getOutputVoltage(), (int)tsacLocation, (int)pccState, (int)pccError);
        lastDebugInfo = millis();
      }
    }

    uint64_t getBitsBigEndian(uint8_t data[8], uint8_t bitStart, uint8_t bitLen)
    {
      uint64_t result = 0;
      for (uint8_t i = 0; i < 8; i++)
      { // First swap endianness
        result |= data[i];
        if (i != 7)
        {
          result <<= 8;
        }
      }
      result <<= bitStart; // Shift out irrelevant bits
      result >>= 64 - bitLen;
      return result;
    }

    double parseDCDCMessage(CAN::Frame *frame)
    {
      uint16_t uhs = (uint16_t)frame->data[7];
      uhs |= ((uint16_t)frame->data[6] & 0x000f) << 8;
      return uhs * 0.25;
    }

    double parseCompMessage(CAN::Frame frame)
    {
      return (double)getBitsBigEndian(frame.data, 16, 16)/10; // Parse the compressor message to get the compressor voltage
    }

    void handleReceivingCANMessage()
    {

      CAN::Frame frame; // Frame to hold the received CAN message
      if (osMessageQueueGet(dcdcQueue, &frame, NULL, 0) == osOK)
      {
        dcdcVoltage = parseDCDCMessage(&frame); // Parse the DCDC message to get the battery voltage
        lastDCDCMessage = millis();             // Update the last DCDC message time
      }

      if (osMessageQueueGet(compressorQueue, &frame, NULL, 0) == osOK)
      {
        compressorVoltage = parseCompMessage(frame); // Parse the compressor message to get the compressor voltage
        lastCompressorMessage = millis();            // Update the last compressor message time
      }
    }

    void handleSendingCANMessage()
    {

      uint32_t static lastPccSendTime = 0;                              // Last time the PCC state was sent over CAN
      if (millis() - lastPccSendTime > DEFAULT_SETTINGS.CAN_PCC_PERIOD) // Check if the PCC state was sent recently
      {
        lastPccSendTime = millis(); // Update the last PCC send time
        // Send current pcc state, error and last precharge time
        CAN::Frame frame;
        frame.id = DEFAULT_SETTINGS.CAN_PCC_ID; // Use the PCC CAN ID
        frame.length = 8;                       // Set the length of the frame to 8 bytes

        frame.data[0] = (uint8_t)pccState;             // Set the PCC state in the first byte
        frame.data[1] = (uint8_t)pccError;             // Set the PCC
        frame.data16[1] = (uint16_t)lastPrechargeTime; // Set the last precharge time in the second byte

        mCan->sendMessage(frame); // Send the frame over CAN
      }
    }

    /*
     * @brief Checks if the TSAC location is known based on the received CAN messages.
     * If a charger message is received, the TSAC is on the charging cart.
     * If both compressor and DCDC messages are received, the TSAC is in the car.
     * @return True if the TSAC location is known, false otherwise.
     */
    bool TSACLocationIsKnown()
    {
      if (Charger::isConnected())
      {
        tsacLocation = TSAC_LOCATION::CHARGING_CART; // If a charger message is received, the TSAC is on the charging cart
        return true;
      }
      else if (lastDCDCMessage != 0)
      {
        tsacLocation = TSAC_LOCATION::CAR; // If both compressor and DCDC messages are received, the TSAC is in the car
        return true;
      }
      else
      {
        PRINTF_ERR_TIMED_VAR(pcc_loc, "[PCC] Location not found \n");
        return false;
      }
    }

    bool checkCANCommunicationTimeout()
    {
      if (tsacLocation == TSAC_LOCATION::CHARGING_CART)
      {
        return (Charger::isConnected()); // If the TSAC is in the charging cart, check for charger timeout
      }
      else if (tsacLocation == TSAC_LOCATION::CAR)
      {\
        // TODO implement compressor timeout check when compressor CAN messages are implemented
        return (millis() - lastDCDCMessage > CAN_TIMEOUT_TIME ||  millis() - lastCompressorMessage > CAN_TIMEOUT_TIME); // If the TSAC is in the car, check for DCDC and compressor timeouts
      }

      return false; // If the TSAC location is unknown, return false
    }

    void estimateTSVoltage()
    {
      if (tsacLocation == TSAC_LOCATION::CHARGING_CART)
      {
        tsVoltage = Charger::getOutputVoltage(); // If the TSAC is in the charging cart, use the charger voltage
      }
      else if (tsacLocation == TSAC_LOCATION::CAR)
      {
          tsVoltage = dcdcVoltage; // If the DCDC voltage is lower than the compressor voltage, use the DCDC voltage
      }
      else
      {
        tsVoltage = 0; // If the TSAC location is unknown, set the voltage to 0
      }
    }
  }

  void setFinishPrechargeCommand(bool command)
  {
    if (pccState != PCC_STATE::PRECHARGING)
    {
      PRINTF_ERR("[PCC] Cannot set finish precharge command, PCC is not in PRECHARGING state\n");
      return; // Do not set the command if the PCC is not in precharging state
    }
    receivedFinishPrechargeCommand = command; // Set the flag to indicate if the finish precharge command was received
    PRINTF_ERR("[PCC] Received finish precharge command\n");
  }

  uint32_t getLastPrechargeTime()
  {
    return lastPrechargeTime; // Return the last precharge time in milliseconds
  }

  void setup(CAN *_Can)
  {

    IO::togglePrechargeAndAIR(false); // Ensure the precharge relay is off at startup

    dcdcQueue = osMessageQueueNew(1, sizeof(CAN::Frame), NULL); // Create a new message queue for DCDC messages
    _Can->addListener(dcdcID, dcdcQueue);                       // Add listener for DCDC messages

    compressorQueue = osMessageQueueNew(1, sizeof(CAN::Frame), NULL); // Create a new message queue for compressor messages
    _Can->addListener(compID, compressorQueue);                       // Add listener for compressor messages

    mCan = _Can; // Set the CAN instance for PCC communication
  }

  void loop()
  {
    handleReceivingCANMessage(); // Handle receiving CAN messages
    estimateTSVoltage();         // Estimate the TS voltage based on the received messages

    if (!IO::getFSDCState() && (pccState == PCC_STATE::PRECHARGING || pccState == PCC_STATE::SUCCESS))
    {
      PRINTF_ERR("[PCC] FSDC is not enabled, returning to IDLE state\n");
      pccState = PCC_STATE::IDLE; // If the FSDC is not enabled, return to startup state
    }

    // PRINTF_ERR("[PCC] Current PCC state: %d, Error: %d, TSAC location: %d, millis: %d\n", (int)pccState, (int)pccError, (int)tsacLocation, millis());
    switch (pccState)
    {
    case PCC_STATE::STARTUP:
      receivedFinishPrechargeCommand = false; // Reset the flag at startup
      IO::togglePrechargeAndAIR(false);
      if (TSACLocationIsKnown() && SlaveController::getState() == SlaveController::RUNNING)
      {
        pccState = PCC_STATE::IDLE; // Transition to idle state if the location is known
      }
      /* code */
      break;
    case PCC_STATE::IDLE:
      receivedFinishPrechargeCommand = false; // Reset the flag in idle state
      IO::togglePrechargeAndAIR(false);
      if (IO::getFSDCState())
      {
        PRINTF_ERR("[PCC] FSDC is enabled, transitioning to precharging state\n");
        pccState = PCC_STATE::PRECHARGING; // Transition to precharging state if the FSDC is enabled
        prechargeStartTime = millis();     // Start time for the precharge toggle
      }
      break;
    case PCC_STATE::PRECHARGING:
      IO::togglePrechargeAndAIR(false);

      // If the FSDC turns off, return to idle state
      if (!IO::getFSDCState())
      {
        PRINTF_INFO("[PCC] FSDC turned off during precharge, returning to idle state\n");
        pccState = PCC_STATE::IDLE;
      }

      // if (checkCANCommunicationTimeout())
      // {
      //   pccError = PCC_ERROR::COMMUNICATION_ERROR; // Set error state to communication error if a timeout occurs
      //   pccState = PCC_STATE::ERROR;
      //   break;
      // }

      if (millis() - prechargeStartTime > PRECHARGE_TIMEOUT)
      {
        PRINTF_ERR("[PCC] Precharge time exceeded %d ms, aborting precharge\n", PRECHARGE_TIMEOUT);
        pccError = PCC_ERROR::TIMEOUT; // Set error state to timeout if precharge time exceeds the limit
        pccState = PCC_STATE::ERROR;
        break;
      }

      // Open PCC and close AIR- if minimum precharge voltage is reached (1s) and received command from Companion
      if ((millis() - prechargeStartTime > 1000) && (tsVoltage > (0.95 * (SlaveController::getPackVoltage() / 1000000))))
      // if ((millis() - prechargeStartTime > 1000) && receivedFinishPrechargeCommand)
      {
        pccState = PCC_STATE::SUCCESS;
        receivedFinishPrechargeCommand = false; // Reset the flag after transitioning to success state
        PRINTF_ERR("[PCC] Precharge successful, transitioning to success state\n");
        lastPrechargeTime = millis() - prechargeStartTime; // Store the last precharge time
        break;
      }

      break;
    case PCC_STATE::SUCCESS:
      receivedFinishPrechargeCommand = false; // Reset the flag in success state
      IO::togglePrechargeAndAIR(true);        // Set the precharge relay to true if the PCC is successful

      // Transition to idle state if the FSDC is disabled
      if (!IO::getFSDCState())
      {
        pccState = PCC_STATE::IDLE;
      }
      break;
    case PCC_STATE::ERROR:
      receivedFinishPrechargeCommand = false; // Reset the flag in error state
      IO::togglePrechargeAndAIR(false);
      break;

    default:
      IO::togglePrechargeAndAIR(false);
      PRINTF_ERR("[PCC] Unknown state: %d\n", pccState);
      break;
    }

    handleSendingCANMessage(); // Handle sending CAN messages based on the current state
    // printDebugInfo(); // Print debug info if the debug level is set
  }

  PCC_STATE getPCCState()
  {
    return pccState; // Return the current PCC state
  }

  PCC_ERROR getPCCError()
  {
    return pccError; // Return the current PCC error state
  }

}
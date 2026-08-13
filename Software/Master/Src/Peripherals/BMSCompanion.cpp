#include "BMSCompanion.h"
#include "bcc/SlaveController.h"
#include "cmsis_os.h"
#include <vector>
#include <string>
#include "USBCOM.h"
#include "FreeRTOS.h"
#include "HelperFunc.h"
#include "CompanionHandler.h"
#include "Charger.h"
#include "PCC.h"
#include "bcc/bcc_utils.h"

#define DEBUG_LVL 0
#include "Debug.h"

using std::string;
using std::vector;

namespace BMSCompanion
{
    // PRIVATE SHIT
    namespace
    {
        CompanionVersion COMPANION_VERSION = {
            .major = 0,
            .minor = 1,
            .patch = 0,
        };

        // FreeRTOS stuff
        osThreadId_t companionTaskHandle;
        const osThreadAttr_t companionTaskAttributes = {
            .name = "companionTask",
            .stack_size = 2048,
            .priority = (osPriority_t)osPriorityNormal,
        };

        // Shared snprintf buffer, statically allocated
        char printBuf[512] = {0};

        bool registerRequestFinished = false;
        bool registerRequestWasNamedService = false;
        SlaveController::RegisterRequest registerRequest;
        SlaveController::RegisterReponse registerReponse;

        uint32_t timeLastSendMessage = 0;
        uint32_t lastSeenMeasurement = 0;

        uint32_t secondaryLoopCounter = 0; // Counter for the secondary loop, used to send messages every 100ms

        enum ServiceResult : uint8_t { SERVICE_OK = 0U, SERVICE_DENIED = 1U, SERVICE_INVALID = 2U };

        void sendServiceResult(uint8_t serviceId, ServiceResult result)
        {
            snprintf(printBuf, sizeof(printBuf), "%02X%01X\n", serviceId, result);
            CompanionHandler::transmitMessage(0x1F, string(printBuf));
        }

        bool isDecimal(const string &value)
        {
            return !value.empty() && value.size() <= 10U &&
                   value.find_first_not_of("0123456789") == string::npos;
        }

        bool parseUnsigned(const string &value, uint8_t base, uint32_t maximum, uint32_t &result)
        {
            if (value.empty())
            {
                return false;
            }
            uint32_t parsed = 0U;
            for (char character : value)
            {
                uint8_t digit = 0U;
                if (character >= '0' && character <= '9') digit = static_cast<uint8_t>(character - '0');
                else if (base == 16U && character >= 'A' && character <= 'F') digit = static_cast<uint8_t>(character - 'A' + 10U);
                else if (base == 16U && character >= 'a' && character <= 'f') digit = static_cast<uint8_t>(character - 'a' + 10U);
                else return false;
                if (digit >= base || parsed > (maximum - digit) / base)
                {
                    return false;
                }
                parsed = parsed * base + digit;
            }
            result = parsed;
            return true;
        }

        // PRIVATE FUNCTIONS
        string encodeGeneralInfoMessage()
        {
            // Message ID 11
            string message = "";

            // Number of slaves
            size_t slaveCount = SlaveController::getNumOfSlaves();
            vector<size_t> cellCountPerSlave = SlaveController::getCellCountPerSlave();
            vector<size_t> ntcCountPerSlave = SlaveController::getNTCCountPerSlave();

            uint16_t cellCount = 0;
            uint16_t ntcCount = 0;
            for (uint8_t i = 0; i < slaveCount; i++)
            {
                cellCount += cellCountPerSlave[i];
                ntcCount += ntcCountPerSlave[i];
            }

            message += toStringFixedWidth(slaveCount, 2);
            message += toStringFixedWidth(cellCount, 3);
            message += toStringFixedWidth(ntcCount, 3);
            message += '\n';
            return message;
        }

        string encodePackState()
        {
            snprintf(printBuf, sizeof(printBuf), "%01x%04x\n", SlaveController::getState(), SlaveController::getFaults());

            return string(printBuf);
        }

        string encodeCellVoltageMessageForSingleCID(const vector<uint32_t> &cellVoltages, const vector<bool> &balancingStates, uint8_t cid)
        {

            string message = toStringFixedWidth(cid, 2);

            for (size_t i = 0; i < cellVoltages.size(); i++)
            {
                snprintf(printBuf, sizeof(printBuf), "%08x%01x", cellVoltages[i], balancingStates[i] ? 1 : 0);
                message += printBuf;
            }

            message += '\n';
            return message;
        }

        string encodeSingleNTCMessage(const vector<uint16_t> &NTCs, uint8_t cid)
        {

            string message = toStringFixedWidth(cid, 2);

            for (size_t i = 0; i < NTCs.size(); i++)
            {
                snprintf(printBuf, sizeof(printBuf), "%04x", NTCs[i]);
                message += printBuf;
            }

            message += '\n';
            return message;
        }

        string encodePackMeasurementsMessage()
        {
            double current = SlaveController::getCurrent();

            snprintf(printBuf, sizeof(printBuf), "%08x%04x%04x\n", SlaveController::getPackVoltage(), (uint16_t) BCC_CURRENT_TO_RAW(current), SlaveController::getSoC());
            return string(printBuf);
        }

        string encodeMaxMinValues()
        {
            snprintf(printBuf, sizeof(printBuf), "%08x%08x%04x%04x\n", SlaveController::getMaxCellVoltage(), SlaveController::getMinCellVoltage(), SlaveController::getMaxNTCtemp(), SlaveController::getMinNTCtemp());

            return string(printBuf);
        }

        string encodeRegisterMessage(uint8_t cid, uint8_t regAddr, uint16_t regValue)
        {
            snprintf(printBuf, sizeof(printBuf), "%02x%02x%04x\n", cid, regAddr, regValue);

            return string(printBuf);
        }

        string encodeICTempsMessage(vector<uint16_t> icTemps)
        {
            string message = "";

            for (size_t i = 0; i < icTemps.size(); i++)
            {
                snprintf(printBuf, sizeof(printBuf), "%04x", icTemps[i]);
                message.append(printBuf);
            }

            message += '\n';
            return message;
        }

        string encodeChargerInfo(bool chargerConnected, bool chargingEnabled, double outputVoltage, double outputCurrent, double maxChargingCurrent)
        {
            snprintf(printBuf, sizeof(printBuf), "%d%d%04x%04x%04x\n", chargerConnected ? 1 : 0, chargingEnabled ? 1 : 0, (uint16_t)(maxChargingCurrent * 10),
                     (uint16_t)(outputVoltage * 10), (uint16_t)(outputCurrent * 10));
            return string(printBuf);
        }

        string encodePCCInfo(PCC::PCC_STATE pccState, PCC::PCC_ERROR pccError, uint32_t lastPrechargeTime)
        {
            snprintf(printBuf, sizeof(printBuf), "%01x%01x%08x\n", pccState, pccError, lastPrechargeTime);
            return string(printBuf);
        }

        void checkRegisterRequest()
        {
            if (registerRequestFinished)
            {
                registerRequestFinished = false;
                if (registerReponse.status == BCC_STATUS_SUCCESS)
                {
                    // PRINTF_INFO("Register value: %d\n", registerReponse.regValue);
                    CompanionHandler::transmitMessage(0x15, encodeRegisterMessage(registerRequest.cid, registerRequest.regAddr, registerReponse.regValue));
                    if (registerRequestWasNamedService)
                    {
                        sendServiceResult(0x15U, SERVICE_OK);
                    }
                }
                else
                {
                    PRINTF_ERR("STATUS: %d\n", registerReponse.status);
                    if (registerRequestWasNamedService)
                    {
                        sendServiceResult(0x15U, SERVICE_DENIED);
                    }
                }
                registerRequestWasNamedService = false;
            }
        }

        void parseIncoming(uint8_t msgID, std::string msgBody)
        {

            if (msgID == 0x15)
            {
                if (msgBody.size() != 4U || msgBody.substr(0, 2).find_first_not_of("0123456789") != string::npos)
                {
                    sendServiceResult(0x15U, SERVICE_INVALID);
                    return;
                }
                uint32_t cidValue = 0U;
                uint32_t regValue = 0U;
                if (!parseUnsigned(msgBody.substr(0, 2), 10U, UINT8_MAX, cidValue) ||
                    !parseUnsigned(msgBody.substr(2, 2), 16U, UINT8_MAX, regValue))
                {
                    sendServiceResult(0x15U, SERVICE_INVALID);
                    return;
                }
                const uint8_t cid = static_cast<uint8_t>(cidValue);
                const uint8_t regAddr = static_cast<uint8_t>(regValue);
                if (cid == 0U || cid > SlaveController::getNumOfSlaves())
                {
                    sendServiceResult(0x15U, SERVICE_INVALID);
                    return;
                }
                registerRequest = {.cid = cid, .regAddr = regAddr};
                if (!SlaveController::requestRegister(registerRequest, &registerRequestFinished, &registerReponse))
                {
                    PRINTF_WARN("[BC] Register request rejected\n");
                    sendServiceResult(0x15U, SERVICE_DENIED);
                }
                else
                {
                    registerRequestWasNamedService = true;
                }
            }
            else if (msgID == 0x19)
            {
                if (!isDecimal(msgBody))
                {
                    sendServiceResult(0x19U, SERVICE_INVALID);
                    return;
                }
                // First get the old time
                tm oldTime;
                getRTCtimeUTC(oldTime);
                char oldTimeStr[40];
                strftime(oldTimeStr, sizeof(oldTimeStr), "%c", &oldTime);

                // Set the new time and print it
                uint32_t unixTime = 0U;
                if (!parseUnsigned(msgBody, 10U, UINT32_MAX, unixTime))
                {
                    sendServiceResult(0x19U, SERVICE_INVALID);
                    return;
                }
                setRTCtime(unixTime);
                tm newTime;
                getRTCtimeUTC(newTime);
                char newTimeStr[40];
                strftime(newTimeStr, sizeof(newTimeStr), "%c", &newTime);
                printf("[CH] Changed UTC time from %s to %s\n", oldTimeStr, newTimeStr);
                sendServiceResult(0x19U, SERVICE_OK);
            }
            else if (msgID == 0x1B)
            {
                USBCOM::print("[CH] Clearing faults\n");
                if (PCC::requestFaultClear())
                {
                    SlaveController::clearFaults();
                    sendServiceResult(0x1BU, SERVICE_OK);
                }
                else
                {
                    sendServiceResult(0x1BU, SERVICE_DENIED);
                }
            }
            else if (msgID == 0x1E)
            {
                if (msgBody.size() != 1U || (msgBody[0] != '0' && msgBody[0] != '1'))
                {
                    sendServiceResult(0x1EU, SERVICE_INVALID);
                    return;
                }
                const bool requested = msgBody[0] == '1';
                if (requested && PCC::isFirmwareUpdateLocked())
                {
                    sendServiceResult(0x1EU, SERVICE_DENIED);
                    return;
                }
                PCC::setRunRequest(requested);
                sendServiceResult(0x1EU, SERVICE_OK);
            }
            else if (msgID == 0x1D)
            {
                bool enabled = msgBody.substr(0, 1) == "1" ? true : false;
                double maxCurrent = ((double)std::stoi(msgBody.substr(1, 4).c_str(), nullptr, 16)) / 10;
                PRINTF_ERR("[BC] Setting charging enabled: %d, max current: %.2f A\n", enabled, maxCurrent);
                Charger::toggleCharging(enabled, maxCurrent);
            }
            else
            {
                PRINTF_WARN("[CH] Received unknown message: %s with ID %i\n", msgBody.c_str(), msgID);
            }
        }

        void task(void *argument)
        {
            uint32_t startTick = osKernelGetTickCount();
            while (true)
            {

                checkRegisterRequest();

                // No point in doing stuff if companion not connected
                if (CompanionHandler::isConnectionAlive())
                {
                    if (SlaveController::isNewDataAvailable(lastSeenMeasurement))
                    {
                        CompanionHandler::transmitMessage(0x11, encodeGeneralInfoMessage());

                        CompanionHandler::transmitMessage(0x12, encodePackMeasurementsMessage());

                        // Send voltages
                        vector<vector<uint32_t>> allCellVoltages = SlaveController::getCellVoltages();
                        vector<vector<bool>> balancingStates = SlaveController::getBalancingList();
                        // vector<vector<bool>> balancingStates(SlaveController::getNumOfSlaves());
                        for (size_t i = 0; i < allCellVoltages.size(); i++)
                        {
                            CompanionHandler::transmitMessage(0x13, encodeCellVoltageMessageForSingleCID(allCellVoltages[i], balancingStates[i], i));
                        }

                        // Send NTC's
                        vector<vector<uint16_t>> allNTCs = SlaveController::getNTCtemps();
                        for (size_t i = 0; i < allNTCs.size(); i++)
                        {
                            CompanionHandler::transmitMessage(0x14, encodeSingleNTCMessage(allNTCs[i], i));
                        }

                        CompanionHandler::transmitMessage(0x16, encodeMaxMinValues());

                        CompanionHandler::transmitMessage(0x17, encodePackState());

                        CompanionHandler::transmitMessage(0x18, encodeICTempsMessage(SlaveController::getICtemps()));

                        CompanionHandler::transmitMessage(0x1A, "");

                        timeLastSendMessage = millis();
                    }
                    else
                    {
                        // No new data, but some stuff we want to send anyway
                        if (millis() - timeLastSendMessage > 500)
                        {
                            CompanionHandler::transmitMessage(0x17, encodePackState());
                            timeLastSendMessage = millis();
                        }
                    }

                    if (millis() - secondaryLoopCounter > 200)
                    {
                        secondaryLoopCounter = millis();

                        CompanionHandler::transmitMessage(0x1D, encodeChargerInfo(Charger::isConnected(), Charger::isChargingEnabled(), Charger::getOutputVoltage(), Charger::getOutputCurrent(), Charger::getMaxChargingCurrent()));

                        CompanionHandler::transmitMessage(0x1C, encodePCCInfo(PCC::getPCCState(), PCC::getPCCError(), PCC::getLastPrechargeTime()));
                    }

                        // Send charger info
                }

                // delay(20);
                // PRINTF_ERR("[BMSCompanion] Task loop\n");
                osDelayUntil(startTick += 20 / portTICK_PERIOD_MS);
            }
        }
    }

    void setup()
    {
        CompanionHandler::setup({
            .deviceName = "BMS",
            .version = COMPANION_VERSION,
            .parseIncomingMessage = parseIncoming,
        });

        companionTaskHandle = osThreadNew(task, NULL, &companionTaskAttributes);
    }

}

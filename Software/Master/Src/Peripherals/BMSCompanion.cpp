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
#include "BoardIO.h"

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
        SlaveController::RegisterRequest registerRequest;
        SlaveController::RegisterReponse registerReponse;

        uint32_t timeLastSendMessage = 0;

        uint32_t secondaryLoopCounter = 0; // Counter for the secondary loop, used to send messages every 100ms

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
            double current = IO::getCurrent();

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
                }
                else
                {
                    PRINTF_ERR("STATUS: %d\n", registerReponse.status);
                }
            }
        }

        void parseIncoming(uint8_t msgID, std::string msgBody)
        {

            if (msgID == 0x15)
            {
                registerRequest = {.cid = (uint8_t)atoi(msgBody.substr(0, 2).c_str()), .regAddr = (uint8_t)std::stoi(msgBody.substr(2, 2).c_str(), nullptr, 16)};
                SlaveController::requestRegister(registerRequest, &registerRequestFinished, &registerReponse);
            }
            else if (msgID == 0x19)
            {
                // First get the old time
                tm oldTime;
                getRTCtimeUTC(oldTime);
                char oldTimeStr[40];
                strftime(oldTimeStr, sizeof(oldTimeStr), "%c", &oldTime);

                // Set the new time and print it
                setRTCtime(std::stoi(msgBody.substr(0, 10)));
                tm newTime;
                getRTCtimeUTC(newTime);
                char newTimeStr[40];
                strftime(newTimeStr, sizeof(newTimeStr), "%c", &newTime);
                printf("[CH] Changed UTC time from %s to %s\n", oldTimeStr, newTimeStr);
            }
            else if (msgID == 0x1B)
            {
                USBCOM::print("[CH] Clearing faults\n");
                SlaveController::clearFaults();
            }
            else if (msgID == 0x1C)
            {
                if (msgBody == "1") {
                    PRINTF_INFO("[CH] Received 'finish Precharge' command\n");
                    PCC::setFinishPrechargeCommand(true);
                } else {
                    PRINTF_ERR("[CH] Received unknown precharge command: %s\n", msgBody.c_str());
                }

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
                    if (SlaveController::isNewDataAvailable())
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

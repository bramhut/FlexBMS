#include "StatusLed.h"

#include "BoardIO.h"
#include "BmsUart.h"
#include "FaultManager.h"
#include "TimeFunctions.h"
#include "bcc/SlaveController.h"
#include "pcc.h"

namespace StatusLed
{
    namespace
    {
        constexpr uint32_t BOOT_ACKNOWLEDGEMENT_MS = 1000U;
        constexpr uint32_t WAITING_HALF_PERIOD_MS = 500U;
        constexpr uint32_t HEARTBEAT_PERIOD_MS = 2000U;
        constexpr uint32_t HEARTBEAT_ON_MS = 100U;
        constexpr uint32_t CODE_FLASH_MS = 150U;
        constexpr uint32_t CODE_GAP_MS = 150U;
        constexpr uint32_t UPDATE_HALF_PERIOD_MS = 125U;

        uint32_t bootStartedMs = 0U;
        bool firmwareUpdateActive = false;
        bool fatalLocalFailure = false;

        bool periodicOn(uint32_t now, uint32_t period, uint32_t onTime)
        {
            return (now % period) < onTime;
        }

        bool codeOn(uint32_t now, uint8_t flashes)
        {
            constexpr uint32_t CODE_PERIOD_MS = 2000U;
            const uint32_t phase = now % CODE_PERIOD_MS;
            for (uint8_t flash = 0U; flash < flashes; ++flash)
            {
                const uint32_t start = static_cast<uint32_t>(flash) *
                    (CODE_FLASH_MS + CODE_GAP_MS);
                if (phase >= start && phase < start + CODE_FLASH_MS)
                {
                    return true;
                }
            }
            return false;
        }

        bool isStartupTransition()
        {
            const PCC::PCC_STATE pccState = PCC::getPCCState();
            return SlaveController::getState() != SlaveController::RUNNING ||
                   pccState == PCC::SELF_TEST ||
                   pccState == PCC::PRECHARGE ||
                   pccState == PCC::CONTACTOR_CLOSE;
        }

        bool hasBlockingFault()
        {
            return FaultManager::hasBlockingErrors();
        }

        void show(bool on, bool red)
        {
            IO::setLEDcolor(red ? RGB_t{kRedBrightness, 0.0, 0.0}
                                : RGB_t{0.0, kGreenBrightness, 0.0});
            IO::setLED(on);
        }
    }

    void setup()
    {
        bootStartedMs = millis();
        firmwareUpdateActive = false;
        fatalLocalFailure = false;
    }

    void update()
    {
        const uint32_t now = millis();

        if (now - bootStartedMs < BOOT_ACKNOWLEDGEMENT_MS)
        {
            show(true, false);
            return;
        }

        if (fatalLocalFailure)
        {
            show(true, true);
            return;
        }

        // BmsUart currently measures CRC-valid Gateway traffic. It is an LED
        // source only until the pending UART-loss safety fault is introduced.
        if (BmsUart::isGatewayLinkLost())
        {
            show(codeOn(now, 3U), true);
            return;
        }

        if (hasBlockingFault())
        {
            show(codeOn(now, 1U), true);
            return;
        }

        if (firmwareUpdateActive || PCC::isFirmwareUpdateLocked())
        {
            show(periodicOn(now, UPDATE_HALF_PERIOD_MS * 2U, UPDATE_HALF_PERIOD_MS), false);
            return;
        }

        if (isStartupTransition())
        {
            show(periodicOn(now, WAITING_HALF_PERIOD_MS * 2U, WAITING_HALF_PERIOD_MS), false);
            return;
        }

        if (PCC::getPCCState() == PCC::RUN)
        {
            show(true, false);
            return;
        }

        show(periodicOn(now, HEARTBEAT_PERIOD_MS, HEARTBEAT_ON_MS), false);
    }

    void setFirmwareUpdateActive(bool active)
    {
        firmwareUpdateActive = active;
    }

    void setFatalLocalFailure(bool active)
    {
        fatalLocalFailure = active;
    }
}

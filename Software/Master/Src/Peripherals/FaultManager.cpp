#include "FaultManager.h"

#include "FreeRTOS.h"
#include "main.h"
#include "pcc.h"
#include "task.h"

namespace FaultManager
{
    namespace
    {
        uint32_t bmsActive = 0U;
        uint32_t bmsLatched = 0U;
        uint32_t hvActive = 0U;
        uint32_t hvLatched = 0U;
        uint32_t warnings = 0U;
        bool startupComplete = false;
        bool hvRunning = false;
        bool critical = false;

        constexpr uint32_t mask(uint8_t bit)
        {
            return static_cast<uint32_t>(1UL << bit);
        }

        BmsState stateLocked()
        {
            if (critical) return BmsState::Critical;
            if ((bmsActive | bmsLatched | hvActive | hvLatched) != 0U) return BmsState::Error;
            if (!startupComplete) return BmsState::Starting;
            return hvRunning ? BmsState::Running : BmsState::Ready;
        }

        void updateError(uint32_t &activeMask, uint32_t &latchedMask, uint32_t bit, bool active)
        {
            bool newlyActive = false;
            taskENTER_CRITICAL();
            newlyActive = active && (activeMask & bit) == 0U;
            if (active)
            {
                activeMask |= bit;
                latchedMask |= bit;
            }
            else
            {
                activeMask &= ~bit;
            }
            taskEXIT_CRITICAL();

            if (newlyActive)
            {
                PCC::forceSafeOffFromFaultManager();
            }
        }
    }

    void setup()
    {
        taskENTER_CRITICAL();
        bmsActive = 0U;
        bmsLatched = 0U;
        hvActive = 0U;
        hvLatched = 0U;
        warnings = 0U;
        startupComplete = false;
        hvRunning = false;
        critical = false;

        if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != RESET ||
            __HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST) != RESET)
        {
            warnings |= mask(static_cast<uint8_t>(Warning::WatchdogReset));
        }
        __HAL_RCC_CLEAR_RESET_FLAGS();
        taskEXIT_CRITICAL();
    }

    void setBmsFault(BmsFault fault, bool active)
    {
        updateError(bmsActive, bmsLatched, mask(static_cast<uint8_t>(fault)), active);
    }

    void setHvFault(HvFault fault, bool active)
    {
        updateError(hvActive, hvLatched, mask(static_cast<uint8_t>(fault)), active);
    }

    void setWarning(Warning warning, bool active)
    {
        const uint32_t bit = mask(static_cast<uint8_t>(warning));
        taskENTER_CRITICAL();
        if (active) warnings |= bit;
        else warnings &= ~bit;
        taskEXIT_CRITICAL();
    }

    void enterCritical()
    {
        taskENTER_CRITICAL();
        critical = true;
        taskEXIT_CRITICAL();
        PCC::forceSafeOffFromFaultManager();
    }

    void setStartupComplete(bool complete)
    {
        taskENTER_CRITICAL();
        startupComplete = complete;
        taskEXIT_CRITICAL();
    }

    void setHvRunning(bool running)
    {
        taskENTER_CRITICAL();
        hvRunning = running;
        taskEXIT_CRITICAL();
    }

    bool acknowledge()
    {
        bool accepted = false;
        taskENTER_CRITICAL();
        if (!critical && (bmsActive | hvActive) == 0U)
        {
            bmsLatched = 0U;
            hvLatched = 0U;
            warnings = 0U;
            accepted = true;
        }
        taskEXIT_CRITICAL();
        return accepted;
    }

    Snapshot getSnapshot()
    {
        Snapshot snapshot = {};
        taskENTER_CRITICAL();
        snapshot.bmsActive = bmsActive;
        snapshot.bmsLatched = bmsLatched;
        snapshot.hvActive = hvActive;
        snapshot.hvLatched = hvLatched;
        snapshot.warnings = warnings;
        snapshot.bmsState = stateLocked();
        taskEXIT_CRITICAL();
        return snapshot;
    }

    bool canEnableHv()
    {
        taskENTER_CRITICAL();
        const bool allowed = !critical && startupComplete &&
                             (bmsActive | bmsLatched | hvActive | hvLatched) == 0U;
        taskEXIT_CRITICAL();
        return allowed;
    }

    bool hasBlockingErrors()
    {
        taskENTER_CRITICAL();
        const bool blocking = critical ||
                              (bmsActive | bmsLatched | hvActive | hvLatched) != 0U;
        taskEXIT_CRITICAL();
        return blocking;
    }
}

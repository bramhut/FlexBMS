#include "Watchdog.h"

#include "iwdg.h"
#include "main.h"

#include <atomic>

namespace Watchdog
{
    namespace
    {
        constexpr uint32_t REFRESH_PERIOD_MS = 100U;

        std::atomic<uint32_t> pccProgress{0U};
        std::atomic<uint32_t> bccProgress{0U};
        uint32_t lastPccProgress = 0U;
        uint32_t lastBccProgress = 0U;
        uint32_t lastRefreshAt = 0U;
        bool monitoringStarted = false;

        void refresh()
        {
            (void)HAL_IWDG_Refresh(&hiwdg);
            lastRefreshAt = HAL_GetTick();
        }
    }

    void setup()
    {
        lastPccProgress = 0U;
        lastBccProgress = 0U;
        monitoringStarted = false;
        refresh();
    }

    void reportPccProgress()
    {
        pccProgress.fetch_add(1U, std::memory_order_relaxed);
    }

    void reportBccProgress()
    {
        bccProgress.fetch_add(1U, std::memory_order_relaxed);
    }

    void loop()
    {
        if (HAL_GetTick() - lastRefreshAt < REFRESH_PERIOD_MS)
        {
            return;
        }

        const uint32_t currentPccProgress = pccProgress.load(std::memory_order_relaxed);
        const uint32_t currentBccProgress = bccProgress.load(std::memory_order_relaxed);
        if (currentPccProgress == 0U || currentBccProgress == 0U)
        {
            return;
        }

        if (monitoringStarted &&
            (currentPccProgress == lastPccProgress || currentBccProgress == lastBccProgress))
        {
            return;
        }

        monitoringStarted = true;
        lastPccProgress = currentPccProgress;
        lastBccProgress = currentBccProgress;
        refresh();
    }
}

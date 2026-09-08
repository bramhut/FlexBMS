#include "Watchdog.h"

#include "BccBreadcrumb.h"
#include "FreeRTOS.h"
#include "iwdg.h"
#include "main.h"
#include "task.h"

#include <atomic>

namespace Watchdog
{
    namespace
    {
        constexpr uint32_t REFRESH_PERIOD_MS = 100U;
        constexpr uint32_t PCC_STALL_CLASSIFICATION_MS = 2U * REFRESH_PERIOD_MS;
        constexpr uint8_t PCC_STALLED = 1U << 0U;
        constexpr uint8_t BCC_STALLED = 1U << 1U;

        std::atomic<uint32_t> pccProgress{0U};
        std::atomic<uint32_t> bccProgress{0U};
        std::atomic<uint32_t> lastPccReportAt{0U};
        std::atomic<uint8_t> stalledSources{0U};
        std::atomic<PccPhase> pccPhase{PccPhase::Uninitialized};
        std::atomic<BccPhase> bccPhase{BccPhase::Uninitialized};
        uint32_t lastPccProgress = 0U;
        uint32_t lastBccProgress = 0U;
        uint32_t lastRefreshAt = 0U;
        std::atomic<bool> monitoringStarted{false};

        void persistState()
        {
            // Both monitored tasks update this one 32-bit backup word. Keep
            // the snapshot coherent and prevent an older task write from
            // erasing a newly classified stall.
            taskENTER_CRITICAL();
            BccBreadcrumb::recordWatchdogState(
                stalledSources.load(std::memory_order_acquire),
                static_cast<uint8_t>(bccPhase.load(std::memory_order_relaxed)),
                static_cast<uint8_t>(pccPhase.load(std::memory_order_relaxed)),
                static_cast<uint8_t>(pccProgress.load(std::memory_order_relaxed)),
                static_cast<uint8_t>(bccProgress.load(std::memory_order_relaxed)),
                monitoringStarted.load(std::memory_order_acquire));
            taskEXIT_CRITICAL();
        }

        void refresh()
        {
            (void)HAL_IWDG_Refresh(&hiwdg);
            lastRefreshAt = HAL_GetTick();
        }
    }

    void setup()
    {
        pccProgress.store(0U, std::memory_order_relaxed);
        bccProgress.store(0U, std::memory_order_relaxed);
        lastPccReportAt.store(HAL_GetTick(), std::memory_order_relaxed);
        stalledSources.store(0U, std::memory_order_relaxed);
        pccPhase.store(PccPhase::Uninitialized, std::memory_order_relaxed);
        bccPhase.store(BccPhase::Uninitialized, std::memory_order_relaxed);
        lastPccProgress = 0U;
        lastBccProgress = 0U;
        monitoringStarted.store(false, std::memory_order_release);
        persistState();
        refresh();
    }

    void reportPccProgress()
    {
        pccProgress.fetch_add(1U, std::memory_order_relaxed);
        lastPccReportAt.store(HAL_GetTick(), std::memory_order_release);
        persistState();
    }

    void reportBccProgress()
    {
        bccProgress.fetch_add(1U, std::memory_order_relaxed);
        const uint32_t now = HAL_GetTick();
        if (monitoringStarted.load(std::memory_order_acquire) &&
            now - lastPccReportAt.load(std::memory_order_acquire) >= PCC_STALL_CLASSIFICATION_MS)
        {
            stalledSources.fetch_or(PCC_STALLED, std::memory_order_release);
        }
        persistState();
    }

    void setPccPhase(PccPhase phase)
    {
        pccPhase.store(phase, std::memory_order_relaxed);
        persistState();
    }

    void setBccPhase(BccPhase phase)
    {
        bccPhase.store(phase, std::memory_order_relaxed);
        persistState();
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

        if (monitoringStarted.load(std::memory_order_acquire) &&
            (currentPccProgress == lastPccProgress || currentBccProgress == lastBccProgress))
        {
            uint8_t stopped = 0U;
            if (currentPccProgress == lastPccProgress) stopped |= PCC_STALLED;
            if (currentBccProgress == lastBccProgress) stopped |= BCC_STALLED;
            stalledSources.fetch_or(stopped, std::memory_order_release);
            persistState();
            return;
        }

        stalledSources.store(0U, std::memory_order_release);
        monitoringStarted.store(true, std::memory_order_release);
        lastPccProgress = currentPccProgress;
        lastBccProgress = currentBccProgress;
        persistState();
        refresh();
    }
}

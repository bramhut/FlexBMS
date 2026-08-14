#include "flexbms/TimeSync.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"

#include <atomic>
#include <ctime>

namespace FlexBms::TimeSync
{
    namespace
    {
        constexpr const char *kLogTag = "flexbms_time";
        constexpr int64_t kDeliveryRetryUs = 60LL * 1000LL * 1000LL;
        constexpr const char *kServers[] = {
            "0.nl.pool.ntp.org",
            "1.nl.pool.ntp.org",
            "2.nl.pool.ntp.org",
            "3.nl.pool.ntp.org",
        };

        std::atomic<State> state{State::WaitingForNetwork};
        std::atomic<bool> statusChanged{false};
        std::atomic<bool> pending{false};
        std::atomic<bool> deliveryInFlight{false};
        std::atomic<uint32_t> pendingUnixTime{0U};
        std::atomic<uint32_t> pendingGeneration{0U};
        std::atomic<uint32_t> inFlightUnixTime{0U};
        std::atomic<uint32_t> inFlightGeneration{0U};
        std::atomic<uint32_t> lastSyncUnixTime{0U};
        std::atomic<bool> hasLastSync{false};
        bool componentStarted = false;
        bool sntpRunning = false;
        bool stationConnected = false;
        std::atomic<int64_t> nextDeliveryAttemptUs{0};

        void setState(State value)
        {
            if (state.exchange(value) != value) statusChanged.store(true, std::memory_order_release);
        }

        void onSntpSync(struct timeval *)
        {
            const time_t now = std::time(nullptr);
            if (now < 0 || static_cast<uint64_t>(now) > UINT32_MAX)
            {
                ESP_LOGW(kLogTag, "NTP returned a time outside the UART uint32 range");
                return;
            }
            pendingUnixTime.store(static_cast<uint32_t>(now), std::memory_order_release);
            pendingGeneration.fetch_add(1U, std::memory_order_acq_rel);
            pending.store(true, std::memory_order_release);
            nextDeliveryAttemptUs.store(0, std::memory_order_release);
            setState(State::WaitingForStm32);
        }

        bool startSntp()
        {
            esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(4, ESP_SNTP_SERVER_LIST(
                kServers[0], kServers[1], kServers[2], kServers[3]));
            config.start = false;
            config.sync_cb = &onSntpSync;
            if (esp_netif_sntp_init(&config) != ESP_OK)
            {
                ESP_LOGE(kLogTag, "SNTP initialisation failed");
                return false;
            }
            if (esp_netif_sntp_start() == ESP_OK) return true;
            ESP_LOGE(kLogTag, "SNTP start failed");
            esp_netif_sntp_deinit();
            return false;
        }
    }

    bool start()
    {
        componentStarted = true;
        return true;
    }

    void setStationConnected(bool connected)
    {
        if (!componentStarted) return;
        if (connected == stationConnected) return;
        stationConnected = connected;
        if (stationConnected)
        {
            if (!sntpRunning)
            {
                sntpRunning = startSntp();
            }
            setState(pending.load(std::memory_order_acquire) ? State::WaitingForStm32 : State::WaitingForNtp);
        }
        else
        {
            if (sntpRunning)
            {
                esp_netif_sntp_deinit();
                sntpRunning = false;
            }
            setState(State::WaitingForNetwork);
        }
    }

    bool pendingStm32Sync(uint32_t &unixTime)
    {
        if (!stationConnected || !pending.load(std::memory_order_acquire) || deliveryInFlight.load(std::memory_order_acquire) ||
            esp_timer_get_time() < nextDeliveryAttemptUs.load(std::memory_order_acquire))
        {
            return false;
        }
        unixTime = pendingUnixTime.load(std::memory_order_acquire);
        return true;
    }

    void stm32SyncStarted(uint32_t unixTime)
    {
        inFlightUnixTime.store(unixTime, std::memory_order_release);
        inFlightGeneration.store(pendingGeneration.load(std::memory_order_acquire), std::memory_order_release);
        deliveryInFlight.store(true, std::memory_order_release);
    }

    void completeStm32Sync(bool success)
    {
        deliveryInFlight.store(false, std::memory_order_release);
        if (success)
        {
            lastSyncUnixTime.store(inFlightUnixTime.load(std::memory_order_acquire), std::memory_order_release);
            hasLastSync.store(true, std::memory_order_release);
            if (pendingGeneration.load(std::memory_order_acquire) == inFlightGeneration.load(std::memory_order_acquire))
            {
                pending.store(false, std::memory_order_release);
                setState(State::Synchronized);
            }
            else
            {
                setState(stationConnected ? State::WaitingForStm32 : State::WaitingForNetwork);
            }
            return;
        }
        nextDeliveryAttemptUs.store(esp_timer_get_time() + kDeliveryRetryUs, std::memory_order_release);
        setState(stationConnected ? State::WaitingForStm32 : State::WaitingForNetwork);
    }

    Status getStatus()
    {
        return {
            .state = state.load(std::memory_order_acquire),
            .hasLastSync = hasLastSync.load(std::memory_order_acquire),
            .lastSyncUnixS = lastSyncUnixTime.load(std::memory_order_acquire),
        };
    }

    bool consumeStatusChanged() { return statusChanged.exchange(false, std::memory_order_acq_rel); }

    const char *stateName(State value)
    {
        switch (value)
        {
        case State::WaitingForNetwork: return "waiting_for_network";
        case State::WaitingForNtp: return "waiting_for_ntp";
        case State::WaitingForStm32: return "waiting_for_stm32";
        case State::Synchronized: return "synchronized";
        }
        return "waiting_for_network";
    }
}

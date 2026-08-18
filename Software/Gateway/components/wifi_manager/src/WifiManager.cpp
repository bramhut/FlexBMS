#include "flexbms/WifiManager.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "mdns.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace FlexBms::Wifi
{
    namespace
    {
        constexpr const char *kLogTag = "flexbms_wifi";
        constexpr const char *kNvsNamespace = "wifi";
        constexpr const char *kSsidKey = "ssid";
        constexpr const char *kPasswordKey = "password";
        constexpr size_t kMaxSsidBytes = 32U;
        constexpr size_t kMaxPasswordBytes = 63U;
        constexpr uint8_t kProvisioningChannel = 1U;
        constexpr uint8_t kProvisioningMaxClients = 4U;
        constexpr int64_t kInitialRecoveryDelayUs = 30LL * 1000LL * 1000LL;
        constexpr int64_t kRecoveryAccessPointUs = 10LL * 60LL * 1000LL * 1000LL;
        constexpr int64_t kRecoveryStationGapUs = 60LL * 1000LL * 1000LL;
        constexpr int64_t kScanCooldownUs = 10LL * 1000LL * 1000LL;
        constexpr int64_t kScanCompletionTimeoutUs = 12LL * 1000LL * 1000LL;
        constexpr int64_t kMdnsRetryUs = 5LL * 1000LL * 1000LL;
        constexpr uint32_t kRestartDelayMs = 1500U;
        constexpr const char *kMdnsHostname = "flexbms";
        constexpr const char *kMdnsInstanceName = "FlexBMS Gateway";

        struct Credentials
        {
            std::array<char, kMaxSsidBytes + 1U> ssid{};
            std::array<char, kMaxPasswordBytes + 1U> password{};
        };

        // This optional, ignored header is deliberately compiled into the
        // firmware. See FallbackNetworks.local.example.h before creating it.
        struct FallbackNetwork
        {
            const char *ssid;
            const char *password;
        };

#if __has_include("flexbms/FallbackNetworks.local.h")
#include "flexbms/FallbackNetworks.local.h"
#endif

#ifndef FLEXBMS_WIFI_FALLBACK_NETWORKS
#define FLEXBMS_WIFI_FALLBACK_NETWORKS
#endif

        constexpr size_t kFallbackNetworkCapacity = 4U;
        constexpr std::array<FallbackNetwork, kFallbackNetworkCapacity> kFallbackNetworks = {{FLEXBMS_WIFI_FALLBACK_NETWORKS}};
        constexpr size_t kFallbackNetworkCount = kFallbackNetworks.size();
        constexpr size_t kNoFallbackNetwork = kFallbackNetworkCount;

        enum class ScanPurpose : uint8_t { None, UserRequest, NetworkSelection };
        enum class CandidateKind : uint8_t { None, Primary, Fallback };

        std::atomic<State> state{State::Unavailable};
        std::atomic<bool> statusChanged{false};
        std::atomic<bool> accessPointActive{false};
        bool restartScheduled = false;
        bool wifiStarted = false;
        bool mdnsStarted = false;
        std::atomic<bool> mdnsRefreshRequested{false};
        int64_t nextMdnsAttemptUs = 0;
        std::atomic<bool> scanPending{false};
        std::atomic<bool> scanReady{false};
        std::atomic<ScanPurpose> scanPurpose{ScanPurpose::None};
        int64_t stationWindowStartedUs = 0;
        int64_t stationRecoveryDelayUs = kInitialRecoveryDelayUs;
        int64_t accessPointStartedUs = 0;
        int64_t lastScanStartedUs = -kScanCooldownUs;
        std::atomic<int64_t> scanStartedUs{0};
        Credentials primaryCredentials{};
        Credentials activeCredentials{};
        bool primaryConfigured = false;
        bool primaryAuthenticationFailed = false;
        std::array<bool, kFallbackNetworkCount> fallbackAuthenticationFailed{};
        CandidateKind activeCandidate = CandidateKind::None;
        size_t activeFallbackNetwork = kNoFallbackNetwork;
        AccessPoint accessPoint{};
        ScanResults scanResults{};
        // Wi-Fi events run on ESP-IDF's small sys_evt task stack. One scan is
        // active at a time, so this shared buffer avoids placing 20 records on
        // that stack in either the automatic or browser-initiated scan path.
        std::array<wifi_ap_record_t, 20U> scanRecords{};

        void logError(const char *operation, esp_err_t error)
        {
            ESP_LOGE(kLogTag, "%s failed: %s", operation, esp_err_to_name(error));
        }

        bool startMdns()
        {
            if (mdnsStarted) return true;

            esp_err_t result = mdns_init();
            if (result == ESP_OK) result = mdns_hostname_set(kMdnsHostname);
            if (result == ESP_OK) result = mdns_instance_name_set(kMdnsInstanceName);
            if (result != ESP_OK)
            {
                logError("Starting mDNS", result);
                mdns_free();
                return false;
            }

            mdnsStarted = true;
            ESP_LOGI(kLogTag, "mDNS host announced as %s.local", kMdnsHostname);
            return true;
        }

        void stopMdns()
        {
            if (!mdnsStarted) return;
            mdns_free();
            mdnsStarted = false;
            ESP_LOGI(kLogTag, "mDNS host withdrawn while station link is unavailable");
        }

        void refreshMdns(int64_t now)
        {
            // Do this from the main Gateway task, not the small ESP event task.
            // A station reconnect can give us a different address, and keeping a
            // stale mDNS responder was the source of intermittent flexbms.local
            // discovery after link loss.
            if (mdnsRefreshRequested.exchange(false))
            {
                stopMdns();
                nextMdnsAttemptUs = now;
            }
            if (state.load() != State::Connected || mdnsStarted || now < nextMdnsAttemptUs) return;
            if (!startMdns()) nextMdnsAttemptUs = now + kMdnsRetryUs;
        }

        void setState(State value)
        {
            if (state.exchange(value) != value)
            {
                statusChanged.store(true);
            }
        }

        bool readCredentials(Credentials &target, bool &provisioned)
        {
            provisioned = false;
            nvs_handle_t handle{};
            const esp_err_t openResult = nvs_open(kNvsNamespace, NVS_READONLY, &handle);
            if (openResult == ESP_ERR_NVS_NOT_FOUND)
            {
                return true;
            }
            if (openResult != ESP_OK)
            {
                logError("Opening Wi-Fi credentials", openResult);
                return false;
            }

            size_t ssidLength = target.ssid.size();
            esp_err_t result = nvs_get_str(handle, kSsidKey, target.ssid.data(), &ssidLength);
            if (result == ESP_ERR_NVS_NOT_FOUND)
            {
                nvs_close(handle);
                return true;
            }
            if (result == ESP_OK)
            {
                size_t passwordLength = target.password.size();
                result = nvs_get_str(handle, kPasswordKey, target.password.data(), &passwordLength);
            }
            nvs_close(handle);
            if (result != ESP_OK || target.ssid[0] == '\0')
            {
                logError("Reading Wi-Fi credentials", result);
                return false;
            }
            provisioned = true;
            return true;
        }

        bool writeCredentials(const Credentials &source)
        {
            nvs_handle_t handle{};
            esp_err_t result = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
            const bool opened = result == ESP_OK;
            if (result == ESP_OK) result = nvs_set_str(handle, kSsidKey, source.ssid.data());
            if (result == ESP_OK) result = nvs_set_str(handle, kPasswordKey, source.password.data());
            if (result == ESP_OK) result = nvs_commit(handle);
            if (opened) nvs_close(handle);
            if (result != ESP_OK)
            {
                logError("Saving Wi-Fi credentials", result);
                return false;
            }
            return true;
        }

        bool validText(const char *value, size_t maximum, bool allowEmpty)
        {
            if (value == nullptr) return false;
            const size_t length = strnlen(value, maximum + 1U);
            return (allowEmpty || length > 0U) && length <= maximum;
        }

        void restartTask(void *)
        {
            vTaskDelay(pdMS_TO_TICKS(kRestartDelayMs));
            esp_restart();
        }

        bool scheduleRestart()
        {
            if (restartScheduled) return false;
            restartScheduled = true;
            if (xTaskCreate(restartTask, "wifi_restart", 2048U, nullptr, 4U, nullptr) != pdPASS)
            {
                restartScheduled = false;
                ESP_LOGE(kLogTag, "Could not schedule restart after Wi-Fi configuration");
                return false;
            }
            return true;
        }

        void buildAccessPointName()
        {
            std::array<uint8_t, 6U> mac{};
            const esp_err_t result = esp_read_mac(mac.data(), ESP_MAC_WIFI_STA);
            if (result != ESP_OK)
            {
                logError("Reading Wi-Fi MAC", result);
                return;
            }
            std::snprintf(accessPoint.ssid.data(), accessPoint.ssid.size(), "FlexBMS-Setup-%02X%02X%02X", mac[3], mac[4], mac[5]);
            std::snprintf(accessPoint.address.data(), accessPoint.address.size(), "192.168.4.1");
        }

        bool configureAccessPoint()
        {
            wifi_config_t config{};
            std::memcpy(config.ap.ssid, accessPoint.ssid.data(), std::strlen(accessPoint.ssid.data()));
            config.ap.ssid_len = std::strlen(accessPoint.ssid.data());
            config.ap.channel = kProvisioningChannel;
            config.ap.max_connection = kProvisioningMaxClients;
            config.ap.authmode = WIFI_AUTH_OPEN;
            const esp_err_t result = esp_wifi_set_config(WIFI_IF_AP, &config);
            if (result != ESP_OK)
            {
                logError("Configuring Wi-Fi setup AP", result);
                return false;
            }
            return true;
        }

        void captiveDnsTask(void *)
        {
            const int socketFd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
            if (socketFd < 0)
            {
                ESP_LOGE(kLogTag, "Could not create captive DNS socket");
                vTaskDelete(nullptr);
                return;
            }
            sockaddr_in listenAddress{};
            listenAddress.sin_family = AF_INET;
            listenAddress.sin_addr.s_addr = htonl(INADDR_ANY);
            listenAddress.sin_port = htons(53U);
            if (bind(socketFd, reinterpret_cast<sockaddr *>(&listenAddress), sizeof(listenAddress)) != 0)
            {
                ESP_LOGE(kLogTag, "Could not bind captive DNS socket");
                close(socketFd);
                vTaskDelete(nullptr);
                return;
            }

            std::array<uint8_t, 512U> request{};
            std::array<uint8_t, 512U> response{};
            while (true)
            {
                sockaddr_in remote{};
                socklen_t remoteLength = sizeof(remote);
                const int bytes = recvfrom(socketFd, request.data(), request.size(), 0,
                                           reinterpret_cast<sockaddr *>(&remote), &remoteLength);
                if (bytes < 17 || !accessPointActive.load()) continue;
                if (request[4] != 0U || request[5] != 1U) continue;

                size_t offset = 12U;
                while (offset < static_cast<size_t>(bytes) && request[offset] != 0U)
                {
                    const size_t labelLength = request[offset];
                    if (labelLength == 0U || labelLength > 63U || offset + labelLength >= static_cast<size_t>(bytes))
                    {
                        offset = 0U;
                        break;
                    }
                    offset += labelLength + 1U;
                }
                if (offset == 0U || offset + 5U > static_cast<size_t>(bytes)) continue;
                ++offset;
                const uint16_t queryType = static_cast<uint16_t>(request[offset] << 8U) | request[offset + 1U];
                if (queryType != 1U || offset + 4U > static_cast<size_t>(bytes) || offset + 4U + 16U > response.size()) continue;
                const size_t questionEnd = offset + 4U;

                std::memcpy(response.data(), request.data(), questionEnd);
                response[2] = 0x81U;
                response[3] = 0x80U;
                response[6] = 0x00U;
                response[7] = 0x01U;
                response[8] = response[9] = response[10] = response[11] = 0U;
                size_t answer = questionEnd;
                const uint8_t record[] = {0xC0U, 0x0CU, 0x00U, 0x01U, 0x00U, 0x01U, 0x00U, 0x00U,
                                          0x00U, 0x1EU, 0x00U, 0x04U, 192U, 168U, 4U, 1U};
                std::memcpy(response.data() + answer, record, sizeof(record));
                (void)sendto(socketFd, response.data(), questionEnd + sizeof(record), 0,
                             reinterpret_cast<sockaddr *>(&remote), remoteLength);
            }
        }

        void startCaptiveDns()
        {
            static bool started = false;
            if (!started && xTaskCreate(captiveDnsTask, "captive_dns", 3072U, nullptr, 3U, nullptr) == pdPASS)
            {
                started = true;
            }
        }

        bool startAccessPoint(bool recovery)
        {
            if (!configureAccessPoint()) return false;
            // Scanning is a station-mode operation in ESP-IDF. Provisioning
            // retains an idle STA interface so the Companion can scan while
            // the setup AP remains available; no connection is attempted
            // until credentials exist.
            esp_err_t result = esp_wifi_set_mode(WIFI_MODE_APSTA);
            if (result == ESP_OK && !wifiStarted)
            {
                result = esp_wifi_start();
                wifiStarted = result == ESP_OK;
            }
            if (result != ESP_OK)
            {
                logError("Starting Wi-Fi setup AP", result);
                return false;
            }
            accessPoint.active = true;
            accessPointActive.store(true);
            accessPointStartedUs = esp_timer_get_time();
            startCaptiveDns();
            setState(recovery ? State::Recovery : State::Provisioning);
            ESP_LOGW(kLogTag, "%s AP started; connect to %s and open http://%s", recovery ? "Wi-Fi recovery" : "Wi-Fi setup",
                     accessPoint.ssid.data(), accessPoint.address.data());
            return true;
        }

        bool stopAccessPoint()
        {
            const esp_err_t result = esp_wifi_set_mode(WIFI_MODE_STA);
            if (result != ESP_OK)
            {
                logError("Stopping Wi-Fi setup AP", result);
                return false;
            }
            accessPoint.active = false;
            accessPointActive.store(false);
            return true;
        }

        void finishScan(bool successful)
        {
            scanResults.successful = successful;
            if (!successful) scanResults.count = 0U;
            scanPending.store(false, std::memory_order_release);
            scanPurpose.store(ScanPurpose::None, std::memory_order_release);
            scanReady.store(true, std::memory_order_release);
        }

        void storeScanResults()
        {
            // A delayed SCAN_DONE after a timed-out scan must not satisfy a
            // later request or leave its browser request unresolved.
            if (!scanPending.load(std::memory_order_acquire)) return;
            uint16_t available = 0U;
            if (esp_wifi_scan_get_ap_num(&available) != ESP_OK)
            {
                finishScan(false);
                return;
            }
            const uint16_t count = std::min<uint16_t>(available, scanResults.networks.size());
            uint16_t received = count;
            if (received > 0U && esp_wifi_scan_get_ap_records(&received, scanRecords.data()) != ESP_OK)
            {
                finishScan(false);
                return;
            }
            scanResults.count = received;
            for (uint16_t index = 0U; index < received; ++index)
            {
                auto &target = scanResults.networks[index];
                target = {};
                const size_t length = strnlen(reinterpret_cast<const char *>(scanRecords[index].ssid), target.ssid.size() - 1U);
                std::memcpy(target.ssid.data(), scanRecords[index].ssid, length);
                target.rssi = scanRecords[index].rssi;
                target.secure = scanRecords[index].authmode != WIFI_AUTH_OPEN;
            }
            finishScan(true);
        }

        bool isAuthenticationFailure(uint8_t reason)
        {
            return reason == WIFI_REASON_AUTH_EXPIRE || reason == WIFI_REASON_AUTH_FAIL ||
                   reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT;
        }

        bool isVisible(const wifi_ap_record_t &record, const char *ssid)
        {
            return ssid != nullptr && ssid[0] != '\0' &&
                   std::strncmp(reinterpret_cast<const char *>(record.ssid), ssid, kMaxSsidBytes) == 0;
        }

        bool startCandidate(const Credentials &candidate, CandidateKind kind, size_t fallbackNetwork = kNoFallbackNetwork)
        {
            wifi_config_t config{};
            std::memcpy(config.sta.ssid, candidate.ssid.data(), std::strlen(candidate.ssid.data()));
            std::memcpy(config.sta.password, candidate.password.data(), std::strlen(candidate.password.data()));
            const esp_err_t result = esp_wifi_set_config(WIFI_IF_STA, &config);
            if (result != ESP_OK)
            {
                logError("Selecting Wi-Fi network", result);
                return false;
            }
            activeCredentials = candidate;
            activeCandidate = kind;
            activeFallbackNetwork = fallbackNetwork;
            setState(State::Connecting);
            ESP_LOGI(kLogTag, "Connecting to selected %s network", kind == CandidateKind::Primary ? "primary" : "fallback");
            if (esp_wifi_connect() != ESP_OK)
            {
                ESP_LOGE(kLogTag, "Starting Wi-Fi connection failed");
                return false;
            }
            return true;
        }

        bool selectVisibleNetwork(const std::array<wifi_ap_record_t, 20U> &records, size_t count)
        {
            if (primaryConfigured && !primaryAuthenticationFailed)
            {
                for (size_t index = 0U; index < count; ++index)
                {
                    if (isVisible(records[index], primaryCredentials.ssid.data()))
                    {
                        return startCandidate(primaryCredentials, CandidateKind::Primary);
                    }
                }
            }

            for (size_t fallback = 0U; fallback < kFallbackNetworkCount; ++fallback)
            {
                const auto &network = kFallbackNetworks[fallback];
                if (network.ssid == nullptr || network.ssid[0] == '\0' || network.password == nullptr ||
                    fallbackAuthenticationFailed[fallback])
                {
                    continue;
                }
                for (size_t index = 0U; index < count; ++index)
                {
                    if (!isVisible(records[index], network.ssid)) continue;
                    Credentials candidate{};
                    std::strncpy(candidate.ssid.data(), network.ssid, candidate.ssid.size() - 1U);
                    std::strncpy(candidate.password.data(), network.password, candidate.password.size() - 1U);
                    return startCandidate(candidate, CandidateKind::Fallback, fallback);
                }
            }
            return false;
        }

        void startNetworkSelection()
        {
            if (state.load() == State::Connected || scanPending.load(std::memory_order_acquire)) return;

            wifi_scan_config_t config{};
            const int64_t now = esp_timer_get_time();
            scanPurpose.store(ScanPurpose::NetworkSelection, std::memory_order_release);
            scanPending.store(true, std::memory_order_release);
            scanStartedUs.store(now, std::memory_order_release);
            const esp_err_t result = esp_wifi_scan_start(&config, false);
            if (result != ESP_OK)
            {
                scanPending.store(false, std::memory_order_release);
                scanPurpose.store(ScanPurpose::None, std::memory_order_release);
                logError("Scanning for configured Wi-Fi networks", result);
                (void)startAccessPoint(primaryConfigured);
                return;
            }
            stationWindowStartedUs = now;
            setState(State::Connecting);
            ESP_LOGI(kLogTag, "Scanning for primary and fallback Wi-Fi networks");
        }

        void finishNetworkSelection()
        {
            uint16_t available = 0U;
            if (esp_wifi_scan_get_ap_num(&available) != ESP_OK)
            {
                available = 0U;
            }
            const uint16_t capped = std::min<uint16_t>(available, 20U);
            uint16_t received = capped;
            if (received > 0U && esp_wifi_scan_get_ap_records(&received, scanRecords.data()) != ESP_OK)
            {
                received = 0U;
            }
            scanPending.store(false, std::memory_order_release);
            scanPurpose.store(ScanPurpose::None, std::memory_order_release);
            if (!selectVisibleNetwork(scanRecords, received))
            {
                ESP_LOGW(kLogTag, "No visible primary or fallback Wi-Fi network");
                (void)startAccessPoint(primaryConfigured);
            }
        }

        void onWifiEvent(void *, esp_event_base_t eventBase, int32_t eventId, void *eventData)
        {
            if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_START)
            {
                setState(State::Connecting);
            }
            else if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_DISCONNECTED)
            {
                const auto *event = static_cast<const wifi_event_sta_disconnected_t *>(eventData);
                if (isAuthenticationFailure(event->reason))
                {
                    if (activeCandidate == CandidateKind::Primary) primaryAuthenticationFailed = true;
                    if (activeCandidate == CandidateKind::Fallback && activeFallbackNetwork < kFallbackNetworkCount)
                    {
                        fallbackAuthenticationFailed[activeFallbackNetwork] = true;
                    }
                }
                if (state.load() == State::Connected)
                {
                    // A later link loss starts a new priority cycle: retry the
                    // saved NVS network before any fallback again.
                    primaryAuthenticationFailed = false;
                    fallbackAuthenticationFailed.fill(false);
                }
                activeCandidate = CandidateKind::None;
                activeFallbackNetwork = kNoFallbackNetwork;
                setState(State::Connecting);
                mdnsRefreshRequested.store(true);
                ESP_LOGW(kLogTag, "Wi-Fi disconnected (reason %u); selecting a visible network", event->reason);
                if (!accessPointActive.load()) startNetworkSelection();
            }
            else if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_SCAN_DONE)
            {
                if (scanPurpose.load(std::memory_order_acquire) == ScanPurpose::NetworkSelection)
                {
                    finishNetworkSelection();
                }
                else
                {
                    storeScanResults();
                }
            }
            else if (eventBase == IP_EVENT && eventId == IP_EVENT_STA_GOT_IP)
            {
                if (accessPointActive.load()) (void)stopAccessPoint();
                setState(State::Connected);
                const auto *event = static_cast<const ip_event_got_ip_t *>(eventData);
                ESP_LOGI(kLogTag, "Wi-Fi connected, IP " IPSTR, IP2STR(&event->ip_info.ip));
                mdnsRefreshRequested.store(true);
            }
        }

        bool initialiseNetworkStack()
        {
            esp_err_t result = esp_netif_init();
            if (result != ESP_OK && result != ESP_ERR_INVALID_STATE)
            {
                logError("Initialising network interface", result);
                return false;
            }
            result = esp_event_loop_create_default();
            if (result != ESP_OK && result != ESP_ERR_INVALID_STATE)
            {
                logError("Creating Wi-Fi event loop", result);
                return false;
            }
            if (esp_netif_create_default_wifi_ap() == nullptr || esp_netif_create_default_wifi_sta() == nullptr)
            {
                ESP_LOGE(kLogTag, "Creating Wi-Fi network interfaces failed");
                return false;
            }
            wifi_init_config_t initConfig = WIFI_INIT_CONFIG_DEFAULT();
            result = esp_wifi_init(&initConfig);
            if (result != ESP_OK)
            {
                logError("Initialising Wi-Fi", result);
                return false;
            }
            result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
            if (result == ESP_OK) result = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &onWifiEvent, nullptr);
            if (result == ESP_OK) result = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &onWifiEvent, nullptr);
            if (result != ESP_OK)
            {
                logError("Registering Wi-Fi events", result);
                return false;
            }
            buildAccessPointName();
            return true;
        }

        bool startStation()
        {
            esp_err_t result = esp_wifi_set_mode(WIFI_MODE_STA);
            if (result == ESP_OK) result = esp_wifi_start();
            if (result != ESP_OK)
            {
                logError("Starting station Wi-Fi", result);
                return false;
            }
            wifiStarted = true;
            stationWindowStartedUs = esp_timer_get_time();
            stationRecoveryDelayUs = kInitialRecoveryDelayUs;
            setState(State::Connecting);
            ESP_LOGI(kLogTag, "Wi-Fi station started");
            startNetworkSelection();
            return true;
        }
    }

    bool start()
    {
        setState(State::Unavailable);
        const esp_err_t nvsResult = nvs_flash_init();
        if (nvsResult != ESP_OK)
        {
            logError("Initialising Wi-Fi NVS", nvsResult);
            return false;
        }
        if (!initialiseNetworkStack()) return false;

        bool provisioned = false;
        if (!readCredentials(primaryCredentials, provisioned)) return false;
        primaryConfigured = provisioned;
        // Start STA even without NVS credentials so local fallback entries are
        // considered before the first-time setup AP.
        return startStation();
    }

    void tick()
    {
        const int64_t now = esp_timer_get_time();
        refreshMdns(now);
        if (scanPending.load(std::memory_order_acquire) && now - scanStartedUs.load(std::memory_order_acquire) >= kScanCompletionTimeoutUs)
        {
            ESP_LOGW(kLogTag, "Wi-Fi scan timed out");
            (void)esp_wifi_scan_stop();
            if (scanPurpose.load(std::memory_order_acquire) == ScanPurpose::NetworkSelection)
            {
                scanPending.store(false, std::memory_order_release);
                scanPurpose.store(ScanPurpose::None, std::memory_order_release);
                (void)startAccessPoint(primaryConfigured);
            }
            else
            {
                finishScan(false);
            }
        }
        const State current = state.load();
        if (current == State::Connecting)
        {
            if (stationWindowStartedUs != 0 && now - stationWindowStartedUs >= stationRecoveryDelayUs)
            {
                (void)startAccessPoint(true);
            }
        }
        else if (current == State::Recovery && now - accessPointStartedUs >= kRecoveryAccessPointUs)
        {
            if (stopAccessPoint())
            {
                stationWindowStartedUs = now;
                stationRecoveryDelayUs = kRecoveryStationGapUs;
                startNetworkSelection();
            }
        }
    }

    State getState() { return state.load(); }
    bool consumeStatusChanged() { return statusChanged.exchange(false); }
    const char *getStationSsid() { return activeCredentials.ssid.data(); }
    AccessPoint getAccessPoint() { return accessPoint; }
    bool isAccessPointActive() { return accessPointActive.load(); }
    bool allowsBmsServices() { return state.load() == State::Connected; }

    bool configure(const char *ssid, const char *password)
    {
        if (!validText(ssid, kMaxSsidBytes, false) || !validText(password, kMaxPasswordBytes, true)) return false;
        Credentials replacement{};
        std::strncpy(replacement.ssid.data(), ssid, replacement.ssid.size() - 1U);
        std::strncpy(replacement.password.data(), password, replacement.password.size() - 1U);
        if (!writeCredentials(replacement)) return false;
        primaryCredentials = replacement;
        activeCredentials = replacement;
        primaryConfigured = true;
        return scheduleRestart();
    }

    ScanRequestResult requestScan()
    {
        if (state.load() == State::Unavailable) return ScanRequestResult::Unavailable;
        if (scanPending.load(std::memory_order_acquire) || scanReady.load(std::memory_order_acquire)) return ScanRequestResult::Busy;
        const int64_t now = esp_timer_get_time();
        if (now - lastScanStartedUs < kScanCooldownUs) return ScanRequestResult::RateLimited;
        wifi_scan_config_t config{};
        config.show_hidden = true;
        const esp_err_t result = esp_wifi_scan_start(&config, false);
        if (result == ESP_ERR_WIFI_STATE) return ScanRequestResult::Busy;
        if (result != ESP_OK)
        {
            logError("Starting Wi-Fi scan", result);
            return ScanRequestResult::Unavailable;
        }
        lastScanStartedUs = now;
        scanStartedUs.store(now, std::memory_order_release);
        scanPurpose.store(ScanPurpose::UserRequest, std::memory_order_release);
        scanPending.store(true, std::memory_order_release);
        scanReady.store(false, std::memory_order_release);
        return ScanRequestResult::Started;
    }

    bool consumeScanResults(ScanResults &results)
    {
        if (!scanReady.exchange(false, std::memory_order_acquire)) return false;
        results = scanResults;
        return true;
    }
}

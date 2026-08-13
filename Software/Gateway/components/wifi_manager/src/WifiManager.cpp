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
        constexpr uint32_t kRestartDelayMs = 1500U;
        constexpr const char *kMdnsHostname = "flexbms";
        constexpr const char *kMdnsInstanceName = "FlexBMS Gateway";

        struct Credentials
        {
            std::array<char, kMaxSsidBytes + 1U> ssid{};
            std::array<char, kMaxPasswordBytes + 1U> password{};
        };

        std::atomic<State> state{State::Unavailable};
        std::atomic<bool> statusChanged{false};
        std::atomic<bool> accessPointActive{false};
        bool restartScheduled = false;
        bool wifiStarted = false;
        bool mdnsStarted = false;
        bool scanPending = false;
        bool scanReady = false;
        int64_t stationWindowStartedUs = 0;
        int64_t stationRecoveryDelayUs = kInitialRecoveryDelayUs;
        int64_t accessPointStartedUs = 0;
        int64_t lastScanStartedUs = -kScanCooldownUs;
        Credentials credentials{};
        AccessPoint accessPoint{};
        ScanResults scanResults{};

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

        void storeScanResults()
        {
            uint16_t available = 0U;
            if (esp_wifi_scan_get_ap_num(&available) != ESP_OK)
            {
                scanPending = false;
                return;
            }
            const uint16_t count = std::min<uint16_t>(available, scanResults.networks.size());
            std::array<wifi_ap_record_t, 20U> records{};
            uint16_t received = count;
            if (received > 0U && esp_wifi_scan_get_ap_records(&received, records.data()) != ESP_OK)
            {
                scanPending = false;
                return;
            }
            scanResults.count = received;
            for (uint16_t index = 0U; index < received; ++index)
            {
                auto &target = scanResults.networks[index];
                target = {};
                const size_t length = strnlen(reinterpret_cast<const char *>(records[index].ssid), target.ssid.size() - 1U);
                std::memcpy(target.ssid.data(), records[index].ssid, length);
                target.rssi = records[index].rssi;
                target.secure = records[index].authmode != WIFI_AUTH_OPEN;
            }
            scanPending = false;
            scanReady = true;
        }

        void onWifiEvent(void *, esp_event_base_t eventBase, int32_t eventId, void *eventData)
        {
            if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_START)
            {
                if (credentials.ssid[0] == '\0')
                {
                    setState(State::Provisioning);
                }
                else
                {
                    setState(State::Connecting);
                    const esp_err_t result = esp_wifi_connect();
                    if (result != ESP_OK) logError("Starting Wi-Fi connection", result);
                }
            }
            else if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_DISCONNECTED)
            {
                if (!accessPointActive.load())
                {
                    if (state.load() == State::Connected)
                    {
                        stationWindowStartedUs = esp_timer_get_time();
                        stationRecoveryDelayUs = kInitialRecoveryDelayUs;
                    }
                    setState(State::Connecting);
                }
                const auto *event = static_cast<const wifi_event_sta_disconnected_t *>(eventData);
                ESP_LOGW(kLogTag, "Wi-Fi disconnected (reason %u); reconnecting", event->reason);
                const esp_err_t result = esp_wifi_connect();
                if (result != ESP_OK) logError("Reconnecting Wi-Fi", result);
            }
            else if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_SCAN_DONE)
            {
                storeScanResults();
            }
            else if (eventBase == IP_EVENT && eventId == IP_EVENT_STA_GOT_IP)
            {
                if (accessPointActive.load()) (void)stopAccessPoint();
                setState(State::Connected);
                const auto *event = static_cast<const ip_event_got_ip_t *>(eventData);
                ESP_LOGI(kLogTag, "Wi-Fi connected, IP " IPSTR, IP2STR(&event->ip_info.ip));
                (void)startMdns();
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
            wifi_config_t config{};
            std::memcpy(config.sta.ssid, credentials.ssid.data(), std::strlen(credentials.ssid.data()));
            std::memcpy(config.sta.password, credentials.password.data(), std::strlen(credentials.password.data()));
            esp_err_t result = esp_wifi_set_mode(WIFI_MODE_STA);
            if (result == ESP_OK) result = esp_wifi_set_config(WIFI_IF_STA, &config);
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
        if (!readCredentials(credentials, provisioned)) return false;
        return provisioned ? startStation() : startAccessPoint(false);
    }

    void tick()
    {
        const int64_t now = esp_timer_get_time();
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
                setState(State::Connecting);
            }
        }
    }

    State getState() { return state.load(); }
    bool consumeStatusChanged() { return statusChanged.exchange(false); }
    const char *getStationSsid() { return credentials.ssid.data(); }
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
        credentials = replacement;
        return scheduleRestart();
    }

    ScanRequestResult requestScan()
    {
        if (state.load() == State::Unavailable) return ScanRequestResult::Unavailable;
        if (scanPending) return ScanRequestResult::Busy;
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
        scanPending = true;
        scanReady = false;
        return ScanRequestResult::Started;
    }

    bool consumeScanResults(ScanResults &results)
    {
        if (!scanReady) return false;
        results = scanResults;
        scanReady = false;
        return true;
    }
}

#include "flexbms/WifiManager.h"

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <array>
#include <atomic>
#include <cctype>
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
        constexpr size_t kMaxPasswordBytes = 64U;
        constexpr size_t kMaxFormBytes = 384U;
        constexpr uint8_t kProvisioningChannel = 1U;
        constexpr uint8_t kProvisioningMaxClients = 4U;

        constexpr const char kSetupPage[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>FlexBMS Wi-Fi setup</title></head><body><h1>FlexBMS Wi-Fi setup</h1>
<form method="post" action="/configure"><label>Wi-Fi name (SSID)<br><input name="ssid" maxlength="32" required></label><br>
<label>Password<br><input name="password" type="password" maxlength="64"></label><br><button type="submit">Save and connect</button></form>
<p>This temporary setup network is open and turns off after credentials are saved.</p></body></html>)HTML";

        struct Credentials
        {
            std::array<char, kMaxSsidBytes + 1U> ssid{};
            std::array<char, kMaxPasswordBytes + 1U> password{};
        };

        bool restartScheduled = false;
        std::atomic<State> state{State::Unavailable};

        void logError(const char *operation, esp_err_t error)
        {
            ESP_LOGE(kLogTag, "%s failed: %s", operation, esp_err_to_name(error));
        }

        bool loadCredentials(Credentials &credentials, bool &provisioned)
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

            size_t ssidLength = credentials.ssid.size();
            esp_err_t result = nvs_get_str(handle, kSsidKey, credentials.ssid.data(), &ssidLength);
            if (result == ESP_ERR_NVS_NOT_FOUND)
            {
                nvs_close(handle);
                return true;
            }
            if (result == ESP_OK)
            {
                size_t passwordLength = credentials.password.size();
                result = nvs_get_str(handle, kPasswordKey, credentials.password.data(), &passwordLength);
            }
            nvs_close(handle);

            if (result != ESP_OK || credentials.ssid[0] == '\0')
            {
                logError("Reading Wi-Fi credentials", result);
                return false;
            }
            provisioned = true;
            return true;
        }

        bool saveCredentials(const Credentials &credentials)
        {
            nvs_handle_t handle{};
            esp_err_t result = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
            const bool opened = result == ESP_OK;
            if (result == ESP_OK)
            {
                result = nvs_set_str(handle, kSsidKey, credentials.ssid.data());
            }
            if (result == ESP_OK)
            {
                result = nvs_set_str(handle, kPasswordKey, credentials.password.data());
            }
            if (result == ESP_OK)
            {
                result = nvs_commit(handle);
            }
            if (opened)
            {
                nvs_close(handle);
            }
            if (result != ESP_OK)
            {
                logError("Saving Wi-Fi credentials", result);
                return false;
            }
            return true;
        }

        bool decodeFormValue(const char *source, size_t sourceLength, char *destination, size_t destinationCapacity)
        {
            size_t written = 0U;
            for (size_t index = 0U; index < sourceLength; ++index)
            {
                char value = source[index];
                if (value == '+')
                {
                    value = ' ';
                }
                else if (value == '%')
                {
                    if (index + 2U >= sourceLength || !std::isxdigit(static_cast<unsigned char>(source[index + 1U])) ||
                        !std::isxdigit(static_cast<unsigned char>(source[index + 2U])))
                    {
                        return false;
                    }
                    const auto hexValue = [](char character) -> uint8_t {
                        if (character >= '0' && character <= '9') return static_cast<uint8_t>(character - '0');
                        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
                        return static_cast<uint8_t>(character - 'a' + 10);
                    };
                    value = static_cast<char>((hexValue(source[index + 1U]) << 4U) | hexValue(source[index + 2U]));
                    index += 2U;
                }
                if (value == '\0' || written + 1U >= destinationCapacity)
                {
                    return false;
                }
                destination[written++] = value;
            }
            destination[written] = '\0';
            return true;
        }

        bool readFormField(const char *form, size_t formLength, const char *name, char *destination, size_t destinationCapacity)
        {
            const size_t nameLength = std::strlen(name);
            size_t offset = 0U;
            while (offset < formLength)
            {
                const size_t fieldStart = offset;
                while (offset < formLength && form[offset] != '&') ++offset;
                const size_t fieldEnd = offset;
                if (offset < formLength) ++offset;

                const char *equals = static_cast<const char *>(std::memchr(form + fieldStart, '=', fieldEnd - fieldStart));
                if (equals == nullptr || static_cast<size_t>(equals - (form + fieldStart)) != nameLength ||
                    std::memcmp(form + fieldStart, name, nameLength) != 0)
                {
                    continue;
                }
                const size_t valueStart = static_cast<size_t>(equals - form) + 1U;
                return decodeFormValue(form + valueStart, fieldEnd - valueStart, destination, destinationCapacity);
            }
            return false;
        }

        void restartTask(void *)
        {
            vTaskDelay(pdMS_TO_TICKS(750));
            esp_restart();
        }

        esp_err_t setupPageHandler(httpd_req_t *request)
        {
            httpd_resp_set_type(request, "text/html");
            return httpd_resp_send(request, kSetupPage, HTTPD_RESP_USE_STRLEN);
        }

        esp_err_t configureHandler(httpd_req_t *request)
        {
            if (request->content_len <= 0 || request->content_len > static_cast<int>(kMaxFormBytes))
            {
                return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid form length");
            }

            std::array<char, kMaxFormBytes + 1U> form{};
            size_t received = 0U;
            while (received < static_cast<size_t>(request->content_len))
            {
                const int count = httpd_req_recv(request, form.data() + received,
                                                 static_cast<size_t>(request->content_len) - received);
                if (count <= 0)
                {
                    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Could not read form");
                }
                received += static_cast<size_t>(count);
            }
            form[received] = '\0';

            Credentials credentials{};
            if (!readFormField(form.data(), received, "ssid", credentials.ssid.data(), credentials.ssid.size()) ||
                !readFormField(form.data(), received, "password", credentials.password.data(), credentials.password.size()) ||
                credentials.ssid[0] == '\0')
            {
                return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid SSID or password");
            }
            if (!saveCredentials(credentials))
            {
                return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not save credentials");
            }

            httpd_resp_set_type(request, "text/html");
            const esp_err_t response = httpd_resp_sendstr(request, "Saved. The Gateway will now restart and join Wi-Fi.");
            if (!restartScheduled)
            {
                restartScheduled = true;
                if (xTaskCreate(restartTask, "wifi_restart", 2048U, nullptr, 4U, nullptr) != pdPASS)
                {
                    restartScheduled = false;
                    ESP_LOGE(kLogTag, "Could not schedule reboot after Wi-Fi setup");
                }
            }
            return response;
        }

        bool startSetupServer()
        {
            httpd_config_t config = HTTPD_DEFAULT_CONFIG();
            config.lru_purge_enable = true;
            httpd_handle_t server = nullptr;
            esp_err_t result = httpd_start(&server, &config);
            if (result != ESP_OK)
            {
                logError("Starting Wi-Fi setup server", result);
                return false;
            }

            const httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = setupPageHandler, .user_ctx = nullptr};
            const httpd_uri_t configure = {.uri = "/configure", .method = HTTP_POST, .handler = configureHandler, .user_ctx = nullptr};
            result = httpd_register_uri_handler(server, &root);
            if (result == ESP_OK)
            {
                result = httpd_register_uri_handler(server, &configure);
            }
            if (result != ESP_OK)
            {
                logError("Registering Wi-Fi setup route", result);
                httpd_stop(server);
                return false;
            }
            return true;
        }

        void onWifiEvent(void *, esp_event_base_t eventBase, int32_t eventId, void *eventData)
        {
            if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_START)
            {
                state.store(State::Connecting);
                const esp_err_t result = esp_wifi_connect();
                if (result != ESP_OK)
                {
                    logError("Starting Wi-Fi connection", result);
                }
            }
            else if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_DISCONNECTED)
            {
                state.store(State::Connecting);
                const auto *event = static_cast<const wifi_event_sta_disconnected_t *>(eventData);
                ESP_LOGW(kLogTag, "Wi-Fi disconnected (reason %u); reconnecting", event->reason);
                const esp_err_t result = esp_wifi_connect();
                if (result != ESP_OK)
                {
                    logError("Reconnecting Wi-Fi", result);
                }
            }
            else if (eventBase == IP_EVENT && eventId == IP_EVENT_STA_GOT_IP)
            {
                state.store(State::Connected);
                const auto *event = static_cast<const ip_event_got_ip_t *>(eventData);
                ESP_LOGI(kLogTag, "Wi-Fi connected, IP " IPSTR, IP2STR(&event->ip_info.ip));
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
            return true;
        }

        bool startProvisioningAccessPoint()
        {
            esp_netif_config_t netifConfig = ESP_NETIF_DEFAULT_WIFI_AP();
            esp_netif_t *netif = esp_netif_new(&netifConfig);
            if (netif == nullptr)
            {
                ESP_LOGE(kLogTag, "Creating provisioning network interface failed");
                return false;
            }
            esp_err_t result = esp_netif_attach_wifi_ap(netif);
            if (result == ESP_OK)
            {
                result = esp_wifi_set_default_wifi_ap_handlers();
            }
            if (result != ESP_OK)
            {
                logError("Preparing provisioning network interface", result);
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
            if (result != ESP_OK)
            {
                logError("Selecting Wi-Fi RAM storage", result);
                return false;
            }

            std::array<uint8_t, 6U> mac{};
            result = esp_read_mac(mac.data(), ESP_MAC_WIFI_STA);
            if (result != ESP_OK)
            {
                logError("Reading Wi-Fi MAC", result);
                return false;
            }

            wifi_config_t config{};
            std::snprintf(reinterpret_cast<char *>(config.ap.ssid), sizeof(config.ap.ssid),
                          "FlexBMS-Setup-%02X%02X%02X", mac[3], mac[4], mac[5]);
            config.ap.ssid_len = std::strlen(reinterpret_cast<const char *>(config.ap.ssid));
            config.ap.channel = kProvisioningChannel;
            config.ap.max_connection = kProvisioningMaxClients;
            config.ap.authmode = WIFI_AUTH_OPEN;
            result = esp_wifi_set_mode(WIFI_MODE_AP);
            if (result == ESP_OK) result = esp_wifi_set_config(WIFI_IF_AP, &config);
            if (result == ESP_OK) result = esp_wifi_start();
            if (result != ESP_OK)
            {
                logError("Starting Wi-Fi setup AP", result);
                return false;
            }
            ESP_LOGW(kLogTag, "Wi-Fi setup AP started; connect to %s and open http://192.168.4.1", config.ap.ssid);
            const bool started = startSetupServer();
            state.store(started ? State::Provisioning : State::Unavailable);
            return started;
        }

        bool startStation(const Credentials &credentials)
        {
            esp_netif_config_t netifConfig = ESP_NETIF_DEFAULT_WIFI_STA();
            esp_netif_t *netif = esp_netif_new(&netifConfig);
            if (netif == nullptr)
            {
                ESP_LOGE(kLogTag, "Creating station network interface failed");
                return false;
            }
            esp_err_t result = esp_netif_attach_wifi_station(netif);
            if (result == ESP_OK)
            {
                result = esp_wifi_set_default_wifi_sta_handlers();
            }
            if (result != ESP_OK)
            {
                logError("Preparing station network interface", result);
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
            if (result != ESP_OK)
            {
                logError("Selecting Wi-Fi RAM storage", result);
                return false;
            }
            result = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &onWifiEvent, nullptr);
            if (result == ESP_OK)
            {
                result = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &onWifiEvent, nullptr);
            }

            wifi_config_t config{};
            std::memcpy(config.sta.ssid, credentials.ssid.data(), std::strlen(credentials.ssid.data()));
            std::memcpy(config.sta.password, credentials.password.data(), std::strlen(credentials.password.data()));
            if (result == ESP_OK) result = esp_wifi_set_mode(WIFI_MODE_STA);
            if (result == ESP_OK) result = esp_wifi_set_config(WIFI_IF_STA, &config);
            if (result == ESP_OK) result = esp_wifi_start();
            if (result != ESP_OK)
            {
                logError("Starting station Wi-Fi", result);
                return false;
            }
            ESP_LOGI(kLogTag, "Wi-Fi station started");
            state.store(State::Connecting);
            return true;
        }
    }

    bool start()
    {
        state.store(State::Unavailable);
        const esp_err_t nvsResult = nvs_flash_init();
        if (nvsResult != ESP_OK)
        {
            logError("Initialising Wi-Fi NVS", nvsResult);
            return false;
        }
        if (!initialiseNetworkStack())
        {
            return false;
        }

        Credentials credentials{};
        bool provisioned = false;
        if (!loadCredentials(credentials, provisioned))
        {
            return false;
        }
        return provisioned ? startStation(credentials) : startProvisioningAccessPoint();
    }

    State getState()
    {
        return state.load();
    }
}

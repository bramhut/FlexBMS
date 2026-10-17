#pragma once
#include <cstddef>
#include <cstdint>
using esp_err_t = int;
using esp_event_base_t = const char *;
using nvs_handle_t = unsigned;
constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_MAC_WIFI_STA = 0, MALLOC_CAP_8BIT = 0;
constexpr int NVS_READONLY = 0, NVS_READWRITE = 1, MQTT_EVENT_ANY = -1;
constexpr int MQTT_EVENT_CONNECTED = 1, MQTT_EVENT_DISCONNECTED = 2, MQTT_EVENT_DATA = 3;
using esp_mqtt_client_handle_t = void *;
using EventHandler = void (*)(void *, esp_event_base_t, int32_t, void *);
struct esp_mqtt_event_t {
    esp_mqtt_client_handle_t client = nullptr;
    int topic_len = 0, data_len = 0;
    const char *topic = nullptr, *data = nullptr;
};
using esp_mqtt_event_handle_t = esp_mqtt_event_t *;
struct esp_mqtt_client_config_t {
    struct { struct { const char *uri = nullptr; } address; } broker;
    struct {
        const char *username = nullptr, *client_id = nullptr;
        struct { const char *password = nullptr; } authentication;
    } credentials;
    struct {
        struct { const char *topic = nullptr, *msg = nullptr; int msg_len = 0, qos = 0; bool retain = false; } last_will;
    } session;
};
int64_t esp_timer_get_time();
int esp_mqtt_client_publish(esp_mqtt_client_handle_t, const char *, const char *, int, int, int);
int esp_mqtt_client_enqueue(esp_mqtt_client_handle_t, const char *, const char *, int, int, int, bool);
inline int esp_mqtt_client_get_outbox_size(esp_mqtt_client_handle_t) { return 0; }
esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *);
esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t, int, EventHandler, void *);
inline int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t, const char *, int) { return 0; }
inline esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t) { return ESP_OK; }
inline esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t) { return ESP_OK; }
inline esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t) { return ESP_OK; }
inline size_t heap_caps_get_free_size(int) { return 0; }
inline size_t heap_caps_get_largest_free_block(int) { return 0; }
inline size_t heap_caps_get_minimum_free_size(int) { return 0; }
inline esp_err_t esp_read_mac(uint8_t *mac, int) { for (int i=0; i<6; ++i) mac[i] = static_cast<uint8_t>(i); return ESP_OK; }
inline esp_err_t nvs_open(const char *, int mode, nvs_handle_t *handle) { *handle = 1; return mode == NVS_READONLY ? ESP_FAIL : ESP_OK; }
inline esp_err_t nvs_get_str(nvs_handle_t, const char *, char *, size_t *) { return ESP_FAIL; }
inline esp_err_t nvs_get_u16(nvs_handle_t, const char *, uint16_t *) { return ESP_FAIL; }
inline esp_err_t nvs_set_str(nvs_handle_t, const char *, const char *) { return ESP_OK; }
inline esp_err_t nvs_set_u16(nvs_handle_t, const char *, uint16_t) { return ESP_OK; }
inline esp_err_t nvs_commit(nvs_handle_t) { return ESP_OK; }
inline void nvs_close(nvs_handle_t) {}
#define ESP_LOGW(tag, ...) do { (void)(tag); } while (0)

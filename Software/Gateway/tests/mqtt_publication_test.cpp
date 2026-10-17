#include "flexbms/MqttClient.h"
#include "flexbms/EnergyMeters.h"
#include "mqtt_test_platform.h"
#include "cJSON.h"
#include <cassert>
#include <cstring>
#include <string>
#include <vector>

struct Publication { std::string topic, payload; int qos, retain; };
static std::vector<Publication> publications;
static int64_t nowUs = 0;
static EventHandler handler = nullptr;
static std::string failTopic;
static FlexBms::EnergyMeters::State meterState;
namespace FlexBms::EnergyMeters { State getState() { return meterState; } }
int64_t esp_timer_get_time() { return nowUs; }
esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *) { return &nowUs; }
esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t, int, EventHandler fn, void *)
{ handler = fn; return ESP_OK; }
int esp_mqtt_client_publish(esp_mqtt_client_handle_t, const char *topic, const char *payload, int, int qos, int retain)
{
    if (failTopic == topic) return -1;
    publications.push_back({topic, payload, qos, retain});
    return 0;
}
int esp_mqtt_client_enqueue(esp_mqtt_client_handle_t client, const char *topic, const char *payload, int length, int qos, int retain, bool)
{ return esp_mqtt_client_publish(client, topic, payload, length, qos, retain); }
static void event(int id, const char *topic = "", const char *data = "")
{
    esp_mqtt_event_t e{};
    e.client = &nowUs; e.topic = topic; e.data = data;
    e.topic_len = static_cast<int>(std::strlen(topic));
    e.data_len = static_cast<int>(std::strlen(data));
    handler(nullptr, nullptr, id, &e);
}
static void put32(FlexBms::UartV1::Frame &f, size_t offset, uint32_t value)
{ for (size_t i=0; i<4; ++i) f.payload[offset+i] = static_cast<uint8_t>(value >> (8*i)); }
static FlexBms::UartV1::Frame status(bool fresh = true)
{
    FlexBms::UartV1::Frame f{}; f.type = FlexBms::UartV1::MessageType::Status; f.length = 33;
    f.payload[0] = 2; f.payload[1] = 4;
    f.payload[2] = static_cast<uint8_t>((fresh ? 8U : 0U) | 4U | 64U | 128U);
    put32(f, 25, static_cast<uint32_t>(nowUs / 1000)); return f;
}
static FlexBms::UartV1::Frame pack()
{
    FlexBms::UartV1::Frame f{}; f.type = FlexBms::UartV1::MessageType::Pack; f.length = 27;
    f.payload[0] = 2; put32(f, 1, 500'000'000); put32(f, 5, 2'000'000);
    put32(f, 11, 3'000'000); put32(f, 15, 3'100'000); return f;
}
static void receive(const FlexBms::UartV1::Frame &f)
{ FlexBms::Mqtt::publishFrame(f, static_cast<uint32_t>(nowUs / 1000)); }
static void tick() { FlexBms::Mqtt::tick(true); }
static size_t count(const char *suffix)
{
    size_t n = 0;
    for (const auto &p : publications)
        if (p.topic == std::string("flexbms/flexbms_030405/") + suffix) ++n;
    return n;
}
static cJSON *lastJson(const char *suffix)
{
    for (auto i=publications.rbegin(); i != publications.rend(); ++i)
        if (i->topic == std::string("flexbms/flexbms_030405/") + suffix)
            return cJSON_Parse(i->payload.c_str());
    assert(false); return nullptr;
}
static cJSON *item(cJSON *root, const char *key)
{ auto *value = cJSON_GetObjectItemCaseSensitive(root, key); assert(value); return value; }
int main()
{
    assert(FlexBms::Mqtt::start(nullptr));
    assert(FlexBms::Mqtt::configure("broker.test", 1883, "test", "test"));
    tick(); event(MQTT_EVENT_CONNECTED);
    nowUs = 1'000'000; tick();
    assert(count("state/status") == 0 && count("state/pack") == 0);
    auto *discovery = cJSON_Parse(publications[1].payload.c_str());
    auto *components = item(discovery, "cmps");
    auto *power = item(components, "pack_power");
    assert(std::string(item(power, "stat_t")->valuestring).ends_with("/state/pack"));
    assert(cJSON_IsTrue(item(power, "force_update")));
    assert(item(power, "expire_after")->valueint == 10);
    assert(std::string(item(power, "unique_id")->valuestring) == "flexbms_030405_pack_power");
    auto *run = item(components, "run_request");
    assert(cJSON_GetObjectItemCaseSensitive(run, "force_update") == nullptr);
    assert(item(run, "json_attributes_template") != nullptr);
    assert(cJSON_IsFalse(item(run, "opt")));
    cJSON_Delete(discovery);

    receive(status()); receive(pack()); nowUs = 2'000'000; tick();
    assert(count("state/status") == 1 && count("state/pack") == 1);
    auto *p = lastJson("state/pack");
    assert(item(p, "pack_power_w")->valuedouble == 1000);
    assert(cJSON_GetObjectItemCaseSensitive(p, "run_request") == nullptr);
    cJSON_Delete(p);
    receive(status()); nowUs = 3'000'000; tick();
    assert(count("state/status") == 2 && count("state/pack") == 1);
    // Energy, events and link changes may update diagnostics, never pack/status proof.
    FlexBms::UartV1::Frame e{}; e.type = FlexBms::UartV1::MessageType::Energy; e.length = 17;
    e.payload[0] = 1; receive(e);
    nowUs = 4'000'000; tick();
    assert(count("state/energy") == 1 && count("state/pack") == 1 && count("state/status") == 2);
    e.type = FlexBms::UartV1::MessageType::Event; e.length = 5; receive(e);
    nowUs = 5'000'000; tick();
    assert(count("state/status") == 2 && count("state/pack") == 1);

    // A valid unchanged pack can publish again; malformed PACK cannot.
    receive(status()); receive(pack()); nowUs = 6'000'000; tick();
    assert(count("state/pack") == 2);
    auto bad = pack(); bad.length = 26; receive(bad);
    receive(status()); nowUs = 7'000'000; tick();
    assert(count("state/pack") == 2);
    // Reconnect discards a pending frame, but retains entity/discovery identity.
    receive(pack()); event(MQTT_EVENT_DISCONNECTED); event(MQTT_EVENT_CONNECTED);
    nowUs = 8'000'000; tick();
    assert(count("state/pack") == 2);
    receive(pack()); nowUs = 9'000'000; tick(); // No new STATUS yet.
    assert(count("state/pack") == 2);
    receive(status()); nowUs = 10'000'000; tick();
    assert(count("state/pack") == 2); // New STATUS cannot resurrect the dropped PACK.
    receive(pack()); nowUs = 11'000'000; tick();
    assert(count("state/pack") == 3);
    receive(pack()); event(MQTT_EVENT_DATA, "homeassistant/status", "online");
    nowUs = 12'000'000; tick();
    assert(count("state/pack") == 3);
    receive(status()); receive(pack()); FlexBms::Mqtt::setUartHealthy(true);
    FlexBms::Mqtt::setUartHealthy(false); nowUs = 13'000'000; tick();
    assert(count("state/pack") == 3);
    receive(status()); receive(pack()); receive(status(false)); nowUs = 14'000'000; tick();
    assert(count("state/pack") == 3);
    // Reject aged pending samples rather than stamping them fresh after a delay.
    receive(status()); receive(pack()); nowUs = 18'000'000; tick();
    assert(count("state/pack") == 3);
    receive(status()); receive(pack()); failTopic = "flexbms/flexbms_030405/state/pack";
    nowUs = 19'000'000; tick(); assert(count("state/pack") == 3);
    failTopic.clear(); nowUs = 20'000'000; tick(); assert(count("state/pack") == 4);
    // RS485 meter publishing must coexist with acquisition-based BMS publishing.
    meterState.configuration.meters[0].enabled = true;
    meterState.busReady = true;
    ++meterState.generation;
    auto &meter = meterState.readings[0];
    meter.electricalValid = true;
    meter.electricalUs = 20'000'000;
    meter.totalPowerW = 1234;
    nowUs = 21'000'000; tick();
    assert(count("meter1/state") == 1 && count("state/pack") == 4);
    receive(status()); receive(pack());
    meter.electricalUs = 21'000'000;
    nowUs = 22'000'000; tick();
    assert(count("meter1/state") == 2 && count("state/pack") == 5);
    for (const auto &publication : publications)
        if (publication.topic.find("/state/") != std::string::npos)
            assert(publication.qos == 0 && publication.retain == 0);
}

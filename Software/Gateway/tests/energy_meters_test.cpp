#include "flexbms/MeterJson.h"
#include "flexbms/MeterPublisher.h"
#include <cassert>
#include <limits>
#include <string>
#include <vector>

using namespace FlexBms::EnergyMeters;
static cJSON *get(cJSON *root, const char *key) { return cJSON_GetObjectItemCaseSensitive(root, key); }
static void encode(float value, uint16_t *words)
{
    uint32_t bits; std::memcpy(&bits, &value, sizeof(bits)); words[0] = bits >> 16; words[1] = bits & 65535;
}
static std::string text(cJSON *json)
{
    char *value = cJSON_PrintUnformatted(json); assert(value); std::string result(value); cJSON_free(value); cJSON_Delete(json); return result;
}
struct Message { std::string topic, payload; bool retained; };
static std::vector<Message> messages;
static bool brokerReady = true;
static bool publish(const char *topic, const char *payload, bool retained)
{
    if (!brokerReady) return false;
    messages.push_back({topic, payload, retained}); return true;
}
static bool has(const char *suffix, const char *payload = nullptr)
{
    for (const auto &message : messages)
        if (message.topic.size() >= std::strlen(suffix) && message.topic.compare(message.topic.size() - std::strlen(suffix), std::strlen(suffix), suffix) == 0 &&
            (payload == nullptr || message.payload == payload)) return true;
    return false;
}
int main()
{
    auto config = defaults(); assert(valid(config) && !anyEnabled(config));
    config.meters[1].enabled = true; assert(valid(config) && anyEnabled(config));
    config.meters[0].enabled = true; assert(valid(config));
    config.meters[1].address = 1; assert(!valid(config)); config.meters[1].enabled = false; assert(valid(config));
    config.meters[0].address = 0; assert(!valid(config)); config = defaults();
    config.parity = static_cast<Parity>(3); assert(!valid(config)); config = defaults();
    config.baudRate = 115200; assert(!valid(config)); config = defaults();
    config.stopBits = 0; assert(!valid(config)); config = defaults();
    config.meters[0].name[0] = '\n'; assert(!valid(config));

    cJSON *json = configurationJson(defaults());
    Configuration parsed{}; assert(parseConfiguration(json, parsed) && valid(parsed));
    cJSON *first = cJSON_GetArrayItem(get(json, "meters"), 0);
    cJSON_SetNumberValue(get(first, "address"), 1.5); assert(!parseConfiguration(json, parsed));
    cJSON_SetNumberValue(get(first, "address"), 248); assert(!parseConfiguration(json, parsed));
    cJSON_SetNumberValue(get(first, "address"), 1);
    cJSON_AddNumberToObject(first, "address", 2); assert(!parseConfiguration(json, parsed)); cJSON_Delete(json);
    json = configurationJson(defaults()); cJSON_AddBoolToObject(json, "write_registers", true); assert(!parseConfiguration(json, parsed)); cJSON_Delete(json);
    json = configurationJson(defaults()); cJSON_DeleteItemFromObjectCaseSensitive(json, "parity"); cJSON_AddStringToObject(json, "Parity", "none"); assert(!parseConfiguration(json, parsed)); cJSON_Delete(json);
    json = configurationJson(defaults()); cJSON_DeleteItemFromArray(get(json, "meters"), 1); assert(!parseConfiguration(json, parsed)); cJSON_Delete(json);
    config = defaults(); config.meters[0].enabled = config.meters[1].enabled = true;
    config.meters[1].reversePowerDirection = true; config.parity = Parity::Even; config.stopBits = 2; config.baudRate = 19200;
    json = configurationJson(config); assert(parseConfiguration(json, parsed)); assert(parsed.meters[1].reversePowerDirection && parsed.parity == Parity::Even); cJSON_Delete(json);

    // Explicit Eastron high-word-first examples, including signed export.
    const uint16_t known[] = {0x41A0, 0x0000}; assert(decodeFloat(known) == 20.0F);
    uint16_t phases[12]{}, total[2]{}, counters[4]{};
    for (size_t i = 0; i < 3; ++i) { encode(20.0F + i, phases + i*2); encode(-2300.0F*(i+1), phases + 6+i*2); }
    encode(-13800, total); encode(123.25F, counters); encode(42.5F, counters + 2);
    Reading reading{}; assert(decodeElectrical(phases, total, reading) && decodeEnergy(counters, reading));
    assert(reading.currentA[2] == 22 && reading.powerW[2] == -6900 && reading.totalPowerW == -13800);
    auto reverse = reported(reading, true); assert(reverse.totalPowerW == 13800 && reverse.powerW[0] == 2300 && reverse.currentA == reading.currentA);
    assert(reverse.importKwh == 42.5F && reverse.exportKwh == 123.25F && reading.importKwh == 123.25F);
    encode(std::numeric_limits<float>::quiet_NaN(), total); assert(!decodeElectrical(phases, total, reading) && reading.totalPowerW == -13800);
    encode(-13800, total); encode(-1, phases); assert(!decodeElectrical(phases, total, reading) && reading.currentA[0] == 20);
    encode(std::numeric_limits<float>::infinity(), counters); assert(!decodeEnergy(counters, reading) && reading.importKwh == 123.25F);
    assert(fresh(true, 1'000'000, 3'999'999, kElectricalFreshUs)); assert(!fresh(true, 1'000'000, 4'000'000, kElectricalFreshUs));
    assert(!fresh(false, 1, 2, kElectricalFreshUs) && !fresh(true, 2, 1, kElectricalFreshUs));

    State state{}; state.available = state.busReady = true; state.configuration = config;
    reading.electricalValid = reading.energyValid = true; reading.electricalUs = 1'000'000; reading.energyUs = 1'000'000;
    state.readings[0] = reading;
    json = statusJson(state, 2'000'000); first = cJSON_GetArrayItem(get(json, "meters"), 0);
    assert(cJSON_IsTrue(get(first, "available")) && get(first, "electrical")); cJSON_Delete(json);
    json = statusJson(state, 4'000'000); first = cJSON_GetArrayItem(get(json, "meters"), 0);
    assert(cJSON_IsFalse(get(first, "available")) && get(first, "electrical") == nullptr && get(first, "energy")); cJSON_Delete(json);
    state.readings[0].electricalValid = false;
    json = statusJson(state, 2'000'000); first = cJSON_GetArrayItem(get(json, "meters"), 0);
    assert(get(first, "electrical") == nullptr && get(first, "last_success_age_ms")->valuedouble == 1000); cJSON_Delete(json);
    json = statusJson(state, 91'000'000); assert(!get(cJSON_GetArrayItem(get(json, "meters"), 0), "energy")); cJSON_Delete(json);

    json = discoveryJson(state, 0, "flexbms_AABBCC", "flexbms/flexbms_AABBCC");
    auto component = get(get(json, "cmps"), "current_l1_a");
    const std::string unique = get(component, "unique_id")->valuestring;
    assert(unique == "flexbms_AABBCC_meter1_current_l1_a" && get(component, "expire_after")->valueint == 3);
    auto energy = get(get(json, "cmps"), "import_kwh");
    assert(get(energy, "expire_after")->valueint == 90 && cJSON_GetArraySize(get(energy, "avty")) == 3);
    assert(std::string(get(energy, "state_class")->valuestring) == "total_increasing");
    assert(cJSON_GetArraySize(get(json, "avty")) == 2);
    assert(text(json).find("/meter1/energy") != std::string::npos);
    std::strcpy(state.configuration.meters[0].name.data(), "Schuur feeder"); state.configuration.meters[0].address = 37;
    json = discoveryJson(state, 0, "flexbms_AABBCC", "flexbms/flexbms_AABBCC"); assert(unique == get(get(get(json, "cmps"), "current_l1_a"), "unique_id")->valuestring); cJSON_Delete(json);
    json = discoveryJson(state, 1, "flexbms_AABBCC", "flexbms/flexbms_AABBCC"); assert(unique != get(get(get(json, "cmps"), "current_l1_a"), "unique_id")->valuestring); cJSON_Delete(json);
    Publisher publisher;
    const auto tick = [&](int64_t now, bool force = false) { publisher.tick(state, now, force, "flexbms_AABBCC", "flexbms/flexbms_AABBCC", publish); };
    state.configuration = defaults(); state.configuration.meters[0].enabled = true;
    state.readings[0] = reading; state.readings[0].electricalValid = true; state.readings[0].energyValid = true;
    tick(2'000'000);
    assert(has("_meter1/config") && has("_meter2/config", "") && has("/meter1/availability", "online"));
    assert(has("/meter1/state") && has("/meter1/energy"));
    for (const auto &message : messages) if (message.topic.find("/state") != std::string::npos || message.topic.find("/energy") == message.topic.size() - 7) assert(!message.retained);
    messages.clear(); tick(2'100'000); assert(messages.empty()); // no stale sample republishing
    state.readings[0].energyValid = false;
    tick(2'200'000); assert(has("/meter1/energy_availability", "offline") && !has("/meter1/availability", "offline"));
    messages.clear(); tick(4'000'000); assert(has("/meter1/availability", "offline") && !has("/meter1/state"));
    messages.clear(); tick(4'100'000, true); assert(has("/meter1/availability", "offline") && !has("/meter1/state")); // reconnect never publishes stale data
    state.readings[0].electricalUs = 4'200'000;
    messages.clear(); tick(4'300'000); assert(has("/meter1/state") && has("/meter1/availability", "online"));
    messages.clear(); tick(4'400'000, true); assert(has("_meter1/config") && has("/meter1/state")); // HA birth restores current data
    state.configuration.meters[1].enabled = true; state.readings[1] = state.readings[0]; ++state.generation;
    messages.clear(); tick(4'500'000); assert(has("/meter2/state"));
    state.readings[0].electricalValid = false; state.readings[1].electricalUs = 4'600'000;
    messages.clear(); tick(4'700'000); assert(has("/meter1/availability", "offline") && has("/meter2/state") && !has("/meter2/availability", "offline"));
    state.configuration.meters[0].enabled = false; ++state.generation;
    messages.clear(); tick(4'800'000); assert(has("_meter1/config", "") && !has("/meter1/state"));
    brokerReady = false; state.configuration.meters[0].enabled = true; ++state.generation;
    messages.clear(); tick(4'900'000); assert(messages.empty());
    brokerReady = true; tick(5'000'000); assert(has("_meter1/config") && has("/meter2/state")); // retry failed queue writes
    std::puts("Energy meter model, parser, freshness, discovery and publisher checks passed.");
}

#pragma once
#include "flexbms/MeterModel.h"
#include "cJSON.h"

#include <cstring>

namespace FlexBms::EnergyMeters
{
    // Outbound MQTT buffer of the Gateway client. Meter messages are queued
    // with esp_mqtt_client_enqueue. ESP-IDF 5.5 esp-mqtt leaves
    // fragmented_msg_data_offset stale after enqueueing a message larger than
    // this buffer; the next direct publish then sends surplus payload bytes
    // and the broker rejects the stream ("Invalid QoS", "malformed packet").
    // Queued meter messages must therefore always fit unfragmented.
    inline constexpr size_t kMqttOutBufferBytes = 6144U;

    // Size of an MQTT 3.1.1 QoS 1 PUBLISH, including the worst-case fixed header.
    inline size_t publishPacketBytes(const char *topic, const char *payload)
    {
        return 5U + 2U + std::strlen(topic) + 2U + std::strlen(payload);
    }
    inline bool fitsUnfragmented(const char *topic, const char *payload)
    {
        return publishPacketBytes(topic, payload) <= kMqttOutBufferBytes;
    }

    // Transport-independent publisher, also exercised by native tests.
    class Publisher
    {
    public:
        using Publish = bool (*)(const char *topic, const char *payload, bool retained);
        void tick(const State &meters, int64_t nowUs, bool rediscover, const char *gatewayId, const char *baseTopic, Publish send);
    private:
        bool publishMeterJson(const char *topic, cJSON *json, bool retained);
        Publish publishMeter = nullptr;
        uint32_t meterGeneration = 0;
        std::array<int64_t, kMeterCount> meterElectricalUs{}, meterEnergyUs{};
        std::array<int, kMeterCount> meterOnline{-1, -1}, meterEnergyOnline{-1, -1};
    };
}

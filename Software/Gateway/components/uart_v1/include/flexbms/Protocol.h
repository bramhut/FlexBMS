#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace FlexBms::UartV1
{
    constexpr uint8_t kMagic0 = 0x46U;
    constexpr uint8_t kMagic1 = 0x42U;
    constexpr uint8_t kVersion = 0x01U;
    constexpr size_t kHeaderBytes = 7U;
    constexpr size_t kCrcBytes = 4U;
    constexpr size_t kMaxPayloadBytes = 512U;
    constexpr size_t kMaxFrameBytes = kHeaderBytes + kMaxPayloadBytes + kCrcBytes;

    enum class MessageType : uint8_t
    {
        Heartbeat = 0x01U,
        Status = 0x02U,
        Pack = 0x03U,
        Cell = 0x04U,
        Temperature = 0x05U,
        HvVoltages = 0x06U,
        ServiceRequest = 0x10U,
        ServiceResponse = 0x11U,
        Event = 0x12U,
    };

    struct Frame
    {
        MessageType type{};
        uint8_t sequence{};
        uint16_t length{};
        std::array<uint8_t, kMaxPayloadBytes> payload{};
    };

    struct Status
    {
        uint8_t bmsState{};
        uint8_t hvState{};
        uint16_t flags{};
        uint8_t slaveCount{};
        uint32_t bmsActiveErrors{};
        uint32_t bmsLatchedErrors{};
        uint32_t hvActiveErrors{};
        uint32_t hvLatchedErrors{};
        uint32_t warnings{};
        uint32_t uptimeMs{};
        uint32_t socLastCalibrationUnixS{};
    };

    struct Pack
    {
        uint32_t packVoltageUv{};
        int16_t packCurrentRaw{};
        uint16_t socRaw{};
        uint32_t minCellUv{};
        uint32_t maxCellUv{};
        uint16_t minNtcRaw{};
        uint16_t maxNtcRaw{};
        uint16_t minIcRaw{};
        uint16_t maxIcRaw{};
    };

    struct HvVoltages
    {
        bool valid{};
        uint32_t batteryVoltageUv{};
        uint32_t loadVoltageUv{};
    };

    struct Cell
    {
        uint8_t slaveIndex{};
        uint16_t balanceMask{};
        std::array<uint32_t, 12U> voltageUv{};
    };

    struct Temperature
    {
        uint8_t slaveIndex{};
        std::array<uint16_t, 4U> ntcRaw{};
        uint16_t icRaw{};
    };

    uint32_t crc32(const uint8_t *data, size_t length);

    // Returns the encoded size, or zero when output is too small or input is invalid.
    size_t encode(const Frame &frame, uint8_t *output, size_t outputCapacity);

    class StreamDecoder
    {
    public:
        // Returns true exactly once for every complete, CRC-valid frame.
        bool consume(uint8_t byte, Frame &frame);
        void reset();

    private:
        enum class State : uint8_t { Magic0, Magic1, Body, Crc };

        void resetForNext(uint8_t lastByte = 0U);

        State state_{State::Magic0};
        std::array<uint8_t, kHeaderBytes + kMaxPayloadBytes> body_{};
        std::array<uint8_t, kCrcBytes> receivedCrc_{};
        size_t bodyLength_{};
        uint16_t payloadLength_{};
        uint8_t crcLength_{};
    };

    bool decodeStatus(const Frame &frame, Status &status);
    bool decodePack(const Frame &frame, Pack &pack);
    bool decodeHvVoltages(const Frame &frame, HvVoltages &voltages);
    bool decodeCell(const Frame &frame, Cell &cell);
    bool decodeTemperature(const Frame &frame, Temperature &temperature);

    // Fast boot-time regression check for CRC, framing, fragmentation, and resynchronisation.
    bool verifyCodec();
}

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
        Energy = 0x07U,
        GoodweCanDiagnostics = 0x08U,
        SocCalibration = 0x09U,
        BalancingCharge = 0x0AU,
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
        uint32_t watchdogBreadcrumb{};
        uint32_t watchdogDiagnostic{};
    };

    struct Pack
    {
        uint32_t packVoltageUv{};
        int32_t packCurrentMicroAmps{};
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

    struct SocCalibration
    {
        bool valid{};
        bool preSocValid{};
        uint16_t preSocRaw{};
        uint32_t unixTimeS{};
        uint32_t previousUnixTimeS{};
        uint32_t qualifyingDwellMs{};
        bool selfDischargeAvailable{};
        bool selfDischargeSocValid{};
        uint8_t selfDischargeRateTenthPercentPer30Days{};
        uint32_t selfDischargeEquivalentCurrentMicroAmps{};
        uint64_t selfDischargeAccumulatedSinceCalibrationMicroAh{};
        bool selfDischargeIntervalComplete{};
        uint32_t lastCalibrationSelfDischargeMilliAh{};
        bool lastCalibrationSelfDischargeAvailable{};
        bool lastCalibrationSelfDischargeComplete{};
    };

    struct Energy
    {
        bool valid{};
        uint64_t chargedEnergyUWh{};
        uint64_t dischargedEnergyUWh{};
    };

    struct GoodweTransmitFrameDiagnostics
    {
        uint32_t successCount{};
        uint32_t lastSuccessMs{};
    };

    struct GoodweReceiveFrameDiagnostics
    {
        uint32_t count{};
        uint32_t lastSeenMs{};
        uint8_t length{};
        std::array<uint8_t, 8U> data{};
    };

    struct GoodweCanDiagnostics
    {
        uint8_t violationDirection{};
        int32_t violationCurrentMilliA{};
        uint16_t violationLimitDeciA{};
        uint32_t violationUptimeMs{};
        uint8_t schemaVersion{};
        uint8_t protocol{};
        bool request45aEnabled{};
        bool compatibility460Enabled{};
        uint32_t transmitCycles{};
        uint32_t snapshotUnavailableCycles{};
        uint32_t transmitFailures{};
        uint32_t lastTransmitFailureMs{};
        uint16_t lastTransmitFailureId{};
        int16_t reported458CurrentDeciA{};
        uint16_t reported458VoltageDeciV{};
        uint8_t transmitErrorCount{};
        uint8_t receiveErrorCount{};
        uint8_t errorLoggingCount{};
        uint8_t protocolLastErrorCode{};
        uint8_t protocolActivity{};
        bool errorPassive{};
        bool warning{};
        bool busOff{};
        uint32_t halErrorCode{};
        uint32_t transmitFifoFreeLevel{};
        std::array<GoodweTransmitFrameDiagnostics, 7U> transmitFrames{};
        std::array<GoodweReceiveFrameDiagnostics, 3U> receiveFrames{};
    };

    struct Cell
    {
        uint8_t slaveIndex{};
        // Scheduled balancing-pulse selection, not instantaneous switch state.
        uint16_t balanceMask{};
        std::array<uint32_t, 12U> voltageUv{};
    };

    struct BalancingCharge
    {
        uint8_t slaveIndex{};
        uint8_t cellCount{};
        std::array<uint32_t, 14U> milliAmpHours{};
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
    bool decodeEnergy(const Frame &frame, Energy &energy);
    bool decodeGoodweCanDiagnostics(const Frame &frame, GoodweCanDiagnostics &diagnostics);
    bool decodeSocCalibration(const Frame &frame, SocCalibration &calibration);
    bool decodeCell(const Frame &frame, Cell &cell);
    bool decodeBalancingCharge(const Frame &frame, BalancingCharge &balancingCharge);
    bool decodeTemperature(const Frame &frame, Temperature &temperature);

    // Fast boot-time regression check for CRC, framing, fragmentation, and resynchronisation.
    bool verifyCodec();
}

#include "flexbms/Protocol.h"

#include <algorithm>
#include <cstring>

namespace FlexBms::UartV1
{
    namespace
    {
        uint16_t readLe16(const uint8_t *data)
        {
            return static_cast<uint16_t>(data[0]) |
                   (static_cast<uint16_t>(data[1]) << 8U);
        }

        uint32_t readLe32(const uint8_t *data)
        {
            return static_cast<uint32_t>(data[0]) |
                   (static_cast<uint32_t>(data[1]) << 8U) |
                   (static_cast<uint32_t>(data[2]) << 16U) |
                   (static_cast<uint32_t>(data[3]) << 24U);
        }

        uint64_t readLe64(const uint8_t *data)
        {
            return static_cast<uint64_t>(readLe32(data)) |
                   (static_cast<uint64_t>(readLe32(data + 4U)) << 32U);
        }

        void writeLe16(uint8_t *data, uint16_t value)
        {
            data[0] = static_cast<uint8_t>(value);
            data[1] = static_cast<uint8_t>(value >> 8U);
        }

        void writeLe32(uint8_t *data, uint32_t value)
        {
            data[0] = static_cast<uint8_t>(value);
            data[1] = static_cast<uint8_t>(value >> 8U);
            data[2] = static_cast<uint8_t>(value >> 16U);
            data[3] = static_cast<uint8_t>(value >> 24U);
        }

        bool frameHasPayload(const Frame &frame, MessageType type, size_t length)
        {
            return frame.type == type && frame.length == length;
        }
    }

    uint32_t crc32(const uint8_t *data, size_t length)
    {
        uint32_t crc = 0xFFFFFFFFUL;
        for (size_t i = 0U; i < length; ++i)
        {
            crc ^= data[i];
            for (uint8_t bit = 0U; bit < 8U; ++bit)
            {
                crc = (crc & 1U) != 0U ? (crc >> 1U) ^ 0xEDB88320UL : (crc >> 1U);
            }
        }
        return crc ^ 0xFFFFFFFFUL;
    }

    size_t encode(const Frame &frame, uint8_t *output, size_t outputCapacity)
    {
        const size_t size = kHeaderBytes + frame.length + kCrcBytes;
        if (output == nullptr || frame.length > kMaxPayloadBytes || outputCapacity < size)
        {
            return 0U;
        }

        output[0] = kMagic0;
        output[1] = kMagic1;
        output[2] = kVersion;
        output[3] = static_cast<uint8_t>(frame.type);
        output[4] = frame.sequence;
        writeLe16(output + 5U, frame.length);
        if (frame.length != 0U)
        {
            std::memcpy(output + kHeaderBytes, frame.payload.data(), frame.length);
        }
        writeLe32(output + kHeaderBytes + frame.length, crc32(output, kHeaderBytes + frame.length));
        return size;
    }

    void StreamDecoder::reset()
    {
        resetForNext();
    }

    void StreamDecoder::resetForNext(uint8_t lastByte)
    {
        bodyLength_ = 0U;
        payloadLength_ = 0U;
        crcLength_ = 0U;
        if (lastByte == kMagic0)
        {
            body_[0] = kMagic0;
            bodyLength_ = 1U;
            state_ = State::Magic1;
        }
        else
        {
            state_ = State::Magic0;
        }
    }

    bool StreamDecoder::consume(uint8_t byte, Frame &frame)
    {
        switch (state_)
        {
        case State::Magic0:
            if (byte == kMagic0)
            {
                body_[0] = byte;
                bodyLength_ = 1U;
                state_ = State::Magic1;
            }
            return false;

        case State::Magic1:
            if (byte == kMagic1)
            {
                body_[1] = byte;
                bodyLength_ = 2U;
                state_ = State::Body;
            }
            else
            {
                resetForNext(byte);
            }
            return false;

        case State::Body:
            body_[bodyLength_++] = byte;
            if (bodyLength_ == kHeaderBytes)
            {
                payloadLength_ = readLe16(body_.data() + 5U);
                if (body_[2] != kVersion || payloadLength_ > kMaxPayloadBytes)
                {
                    resetForNext(byte);
                    return false;
                }
                if (payloadLength_ == 0U)
                {
                    state_ = State::Crc;
                }
            }
            else if (bodyLength_ == kHeaderBytes + payloadLength_)
            {
                state_ = State::Crc;
            }
            return false;

        case State::Crc:
            receivedCrc_[crcLength_++] = byte;
            if (crcLength_ != kCrcBytes)
            {
                return false;
            }

            if (readLe32(receivedCrc_.data()) != crc32(body_.data(), bodyLength_))
            {
                resetForNext(byte);
                return false;
            }

            frame.type = static_cast<MessageType>(body_[3]);
            frame.sequence = body_[4];
            frame.length = payloadLength_;
            if (payloadLength_ != 0U)
            {
                std::memcpy(frame.payload.data(), body_.data() + kHeaderBytes, payloadLength_);
            }
            resetForNext();
            return true;
        }
        resetForNext(byte);
        return false;
    }

    bool decodeStatus(const Frame &frame, Status &status)
    {
        if (frame.type != MessageType::Status ||
            (frame.length != 33U && frame.length != 37U && frame.length != 41U)) return false;
        status.bmsState = frame.payload[0];
        status.hvState = frame.payload[1];
        status.flags = readLe16(frame.payload.data() + 2U);
        status.slaveCount = frame.payload[4];
        status.bmsActiveErrors = readLe32(frame.payload.data() + 5U);
        status.bmsLatchedErrors = readLe32(frame.payload.data() + 9U);
        status.hvActiveErrors = readLe32(frame.payload.data() + 13U);
        status.hvLatchedErrors = readLe32(frame.payload.data() + 17U);
        status.warnings = readLe32(frame.payload.data() + 21U);
        status.uptimeMs = readLe32(frame.payload.data() + 25U);
        status.socLastCalibrationUnixS = readLe32(frame.payload.data() + 29U);
        if (frame.length >= 37U && (status.flags & (1U << 9U)) != 0U)
        {
            status.watchdogBreadcrumb = readLe32(frame.payload.data() + 33U);
        }
        if (frame.length == 41U && (status.flags & (1U << 10U)) != 0U)
        {
            status.watchdogDiagnostic = readLe32(frame.payload.data() + 37U);
        }
        return true;
    }

    bool decodePack(const Frame &frame, Pack &pack)
    {
        if (!frameHasPayload(frame, MessageType::Pack, 24U)) return false;
        pack.packVoltageUv = readLe32(frame.payload.data());
        pack.packCurrentRaw = static_cast<int16_t>(readLe16(frame.payload.data() + 4U));
        pack.socRaw = readLe16(frame.payload.data() + 6U);
        pack.minCellUv = readLe32(frame.payload.data() + 8U);
        pack.maxCellUv = readLe32(frame.payload.data() + 12U);
        pack.minNtcRaw = readLe16(frame.payload.data() + 16U);
        pack.maxNtcRaw = readLe16(frame.payload.data() + 18U);
        pack.minIcRaw = readLe16(frame.payload.data() + 20U);
        pack.maxIcRaw = readLe16(frame.payload.data() + 22U);
        return true;
    }

    bool decodeHvVoltages(const Frame &frame, HvVoltages &voltages)
    {
        if (!frameHasPayload(frame, MessageType::HvVoltages, 12U)) return false;
        voltages.valid = (frame.payload[0] & 0x01U) != 0U;
        voltages.batteryVoltageUv = readLe32(frame.payload.data() + 4U);
        voltages.loadVoltageUv = readLe32(frame.payload.data() + 8U);
        return true;
    }

    bool decodeEnergy(const Frame &frame, Energy &energy)
    {
        if (!frameHasPayload(frame, MessageType::Energy, 17U)) return false;
        energy.valid = (frame.payload[0] & 0x01U) != 0U;
        energy.chargedEnergyUWh = readLe64(frame.payload.data() + 1U);
        energy.dischargedEnergyUWh = readLe64(frame.payload.data() + 9U);
        return true;
    }

    bool decodeGoodweCanDiagnostics(const Frame &frame, GoodweCanDiagnostics &diagnostics)
    {
        constexpr size_t headerBytes = 40U;
        constexpr size_t transmitFrameBytes = 8U;
        constexpr size_t receiveFrameBytes = 20U;
        constexpr size_t payloadBytes = headerBytes + 7U * transmitFrameBytes + 3U * receiveFrameBytes;
        if (!frameHasPayload(frame, MessageType::GoodweCanDiagnostics, payloadBytes) || frame.payload[0] != 1U) return false;

        diagnostics = {};
        diagnostics.schemaVersion = frame.payload[0];
        diagnostics.protocol = frame.payload[1];
        diagnostics.request45aEnabled = (frame.payload[2] & (1U << 0U)) != 0U;
        diagnostics.compatibility460Enabled = (frame.payload[2] & (1U << 1U)) != 0U;
        diagnostics.transmitCycles = readLe32(frame.payload.data() + 4U);
        diagnostics.snapshotUnavailableCycles = readLe32(frame.payload.data() + 8U);
        diagnostics.transmitFailures = readLe32(frame.payload.data() + 12U);
        diagnostics.lastTransmitFailureMs = readLe32(frame.payload.data() + 16U);
        diagnostics.lastTransmitFailureId = readLe16(frame.payload.data() + 20U);
        diagnostics.reported458CurrentDeciA = static_cast<int16_t>(readLe16(frame.payload.data() + 22U));
        diagnostics.reported458VoltageDeciV = readLe16(frame.payload.data() + 24U);
        diagnostics.transmitErrorCount = frame.payload[26];
        diagnostics.receiveErrorCount = frame.payload[27];
        diagnostics.errorLoggingCount = frame.payload[28];
        diagnostics.protocolLastErrorCode = frame.payload[29];
        diagnostics.protocolActivity = frame.payload[30];
        diagnostics.errorPassive = (frame.payload[31] & (1U << 0U)) != 0U;
        diagnostics.warning = (frame.payload[31] & (1U << 1U)) != 0U;
        diagnostics.busOff = (frame.payload[31] & (1U << 2U)) != 0U;
        diagnostics.halErrorCode = readLe32(frame.payload.data() + 32U);
        diagnostics.transmitFifoFreeLevel = readLe32(frame.payload.data() + 36U);

        size_t offset = headerBytes;
        for (GoodweTransmitFrameDiagnostics &transmit : diagnostics.transmitFrames)
        {
            transmit.successCount = readLe32(frame.payload.data() + offset);
            transmit.lastSuccessMs = readLe32(frame.payload.data() + offset + 4U);
            offset += transmitFrameBytes;
        }
        for (GoodweReceiveFrameDiagnostics &receive : diagnostics.receiveFrames)
        {
            receive.count = readLe32(frame.payload.data() + offset);
            receive.lastSeenMs = readLe32(frame.payload.data() + offset + 4U);
            receive.length = std::min<uint8_t>(frame.payload[offset + 8U], 8U);
            std::memcpy(receive.data.data(), frame.payload.data() + offset + 9U, receive.data.size());
            offset += receiveFrameBytes;
        }
        return true;
    }

    bool decodeCell(const Frame &frame, Cell &cell)
    {
        if (!frameHasPayload(frame, MessageType::Cell, 51U)) return false;
        cell.slaveIndex = frame.payload[0];
        cell.balanceMask = readLe16(frame.payload.data() + 1U);
        for (size_t index = 0U; index < cell.voltageUv.size(); ++index)
        {
            cell.voltageUv[index] = readLe32(frame.payload.data() + 3U + index * 4U);
        }
        return true;
    }

    bool decodeTemperature(const Frame &frame, Temperature &temperature)
    {
        if (!frameHasPayload(frame, MessageType::Temperature, 11U)) return false;
        temperature.slaveIndex = frame.payload[0];
        for (size_t index = 0U; index < temperature.ntcRaw.size(); ++index)
        {
            temperature.ntcRaw[index] = readLe16(frame.payload.data() + 1U + index * 2U);
        }
        temperature.icRaw = readLe16(frame.payload.data() + 9U);
        return true;
    }

    bool verifyCodec()
    {
        static constexpr std::array<uint8_t, 9U> crcInput = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
        static constexpr std::array<uint8_t, 11U> heartbeat = {
            0x46U, 0x42U, 0x01U, 0x01U, 0x00U, 0x00U, 0x00U, 0x8FU, 0x7AU, 0xFBU, 0x7DU,
        };
        static constexpr std::array<uint8_t, 12U> getStatus = {
            0x46U, 0x42U, 0x01U, 0x10U, 0x2AU, 0x01U, 0x00U, 0x01U, 0x8FU, 0x21U, 0xB2U, 0x4BU,
        };
        static constexpr std::array<uint8_t, 13U> setRunRequest = {
            0x46U, 0x42U, 0x01U, 0x10U, 0x2BU, 0x02U, 0x00U, 0x02U, 0x01U, 0x16U, 0x26U, 0xB1U, 0xDCU,
        };
        static constexpr std::array<uint8_t, 17U> readRegisterResponse = {
            0x46U, 0x42U, 0x01U, 0x11U, 0x2CU, 0x06U, 0x00U, 0x04U, 0x00U, 0x03U, 0x20U, 0x34U, 0x12U, 0x1AU, 0x45U, 0xF6U, 0x9CU,
        };
        if (crc32(crcInput.data(), crcInput.size()) != 0xCBF43926UL)
        {
            return false;
        }

        StreamDecoder decoder;
        Frame received{};
        for (size_t index = 0U; index + 1U < heartbeat.size(); ++index)
        {
            if (decoder.consume(heartbeat[index], received)) return false;
        }
        if (!decoder.consume(heartbeat.back(), received) || received.type != MessageType::Heartbeat ||
            received.sequence != 0U || received.length != 0U)
        {
            return false;
        }

        const auto decodeVector = [&decoder, &received](const auto &bytes) {
            decoder.reset();
            bool complete = false;
            for (uint8_t byte : bytes) complete = decoder.consume(byte, received);
            return complete;
        };
        if (!decodeVector(getStatus) || received.type != MessageType::ServiceRequest || received.sequence != 0x2AU ||
            received.length != 1U || received.payload[0] != 0x01U)
        {
            return false;
        }
        if (!decodeVector(setRunRequest) || received.type != MessageType::ServiceRequest || received.sequence != 0x2BU ||
            received.length != 2U || received.payload[0] != 0x02U || received.payload[1] != 0x01U)
        {
            return false;
        }
        if (!decodeVector(readRegisterResponse) || received.type != MessageType::ServiceResponse || received.sequence != 0x2CU ||
            received.length != 6U || received.payload[0] != 0x04U || received.payload[1] != 0x00U ||
            received.payload[2] != 0x03U || received.payload[3] != 0x20U || received.payload[4] != 0x34U || received.payload[5] != 0x12U)
        {
            return false;
        }

        Frame energyFrame{};
        energyFrame.type = MessageType::Energy;
        energyFrame.length = 17U;
        energyFrame.payload[0] = 1U;
        const uint64_t charged = 0x1122334455667788ULL;
        const uint64_t discharged = 0xFFEEDDCCBBAA0099ULL;
        for (size_t index = 0U; index < 8U; ++index)
        {
            energyFrame.payload[1U + index] = static_cast<uint8_t>(charged >> (index * 8U));
            energyFrame.payload[9U + index] = static_cast<uint8_t>(discharged >> (index * 8U));
        }
        Energy decodedEnergy{};
        if (!decodeEnergy(energyFrame, decodedEnergy) || !decodedEnergy.valid ||
            decodedEnergy.chargedEnergyUWh != charged || decodedEnergy.dischargedEnergyUWh != discharged)
        {
            return false;
        }
        Frame malformedEnergy = energyFrame;
        malformedEnergy.length = 16U;
        if (decodeEnergy(malformedEnergy, decodedEnergy)) return false;

        Frame goodweFrame{};
        goodweFrame.type = MessageType::GoodweCanDiagnostics;
        goodweFrame.length = 156U;
        goodweFrame.payload[0] = 1U;
        goodweFrame.payload[1] = 1U;
        goodweFrame.payload[2] = 3U;
        goodweFrame.payload[4] = 7U;
        goodweFrame.payload[22] = 0xD3U;
        goodweFrame.payload[23] = 0xFFU;
        goodweFrame.payload[40] = 11U;
        goodweFrame.payload[44] = 0x39U;
        goodweFrame.payload[136] = 2U;
        goodweFrame.payload[144] = 4U;
        goodweFrame.payload[145] = 0x34U;
        goodweFrame.payload[146] = 0x12U;
        GoodweCanDiagnostics decodedGoodwe{};
        if (!decodeGoodweCanDiagnostics(goodweFrame, decodedGoodwe) ||
            decodedGoodwe.transmitCycles != 7U || decodedGoodwe.reported458CurrentDeciA != -45 ||
            decodedGoodwe.transmitFrames[0].successCount != 11U || decodedGoodwe.transmitFrames[0].lastSuccessMs != 0x39U ||
            decodedGoodwe.receiveFrames[2].count != 2U || decodedGoodwe.receiveFrames[2].length != 4U ||
            decodedGoodwe.receiveFrames[2].data[0] != 0x34U || decodedGoodwe.receiveFrames[2].data[1] != 0x12U)
        {
            return false;
        }
        goodweFrame.payload[0] = 2U;
        if (decodeGoodweCanDiagnostics(goodweFrame, decodedGoodwe)) return false;

        std::array<uint8_t, kMaxFrameBytes> encoded{};
        const Frame expected{.type = MessageType::Heartbeat, .sequence = 0U, .length = 0U};
        return encode(expected, encoded.data(), encoded.size()) == heartbeat.size() &&
               std::memcmp(encoded.data(), heartbeat.data(), heartbeat.size()) == 0;
    }
}

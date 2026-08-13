#include "flexbms/Protocol.h"

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
        if (!frameHasPayload(frame, MessageType::Status, 17U)) return false;
        status.bmsState = frame.payload[0];
        status.hvState = frame.payload[1];
        status.flags = readLe16(frame.payload.data() + 2U);
        status.slaveCount = frame.payload[4];
        status.bmsActiveFaults = readLe16(frame.payload.data() + 5U);
        status.bmsLatchedFaults = readLe16(frame.payload.data() + 7U);
        status.hvActiveFaults = readLe16(frame.payload.data() + 9U);
        status.hvLatchedFaults = readLe16(frame.payload.data() + 11U);
        status.uptimeMs = readLe32(frame.payload.data() + 13U);
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

        std::array<uint8_t, kMaxFrameBytes> encoded{};
        const Frame expected{.type = MessageType::Heartbeat, .sequence = 0U, .length = 0U};
        return encode(expected, encoded.data(), encoded.size()) == heartbeat.size() &&
               std::memcmp(encoded.data(), heartbeat.data(), heartbeat.size()) == 0;
    }
}


#pragma once

#include "Peripherals/GoodweCanConfig.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace GoodweCan
{
    struct CodecData
    {
        bool commonSafe{};
        bool chargeAllowed{};
        bool dischargeAllowed{};

        bool cellOverVoltage{};
        bool cellUnderVoltage{};
        bool overTemperature{};
        bool underTemperature{};
        bool overCurrent{};
        bool communicationFault{};
        bool internalFault{};

        uint16_t moduleCount{};
        uint16_t socPercent{};
        uint16_t sohPercent{};
        uint16_t chargeVoltageDeciV{};
        uint16_t dischargeVoltageDeciV{};
        uint16_t chargeCurrentDeciA{};
        uint16_t dischargeCurrentDeciA{};
        uint16_t packVoltageDeciV{};
        int16_t packCurrentDeciA{};
        int16_t averageTemperatureDeciC{};
    };

    struct EncodedFrame
    {
        uint32_t id{};
        uint8_t length{8U};
        std::array<uint8_t, 8U> data{};
    };

    namespace detail
    {
        constexpr void putLe16(std::array<uint8_t, 8U> &payload, size_t offset, uint16_t value)
        {
            payload[offset] = static_cast<uint8_t>(value & 0xFFU);
            payload[offset + 1U] = static_cast<uint8_t>(value >> 8U);
        }

        constexpr uint16_t clampUnsigned16(uint32_t value)
        {
            return static_cast<uint16_t>(std::min<uint32_t>(value, 0xFFFFU));
        }

        constexpr int16_t clampSigned16(int32_t value)
        {
            return static_cast<int16_t>(std::clamp<int32_t>(
                value,
                std::numeric_limits<int16_t>::min(),
                std::numeric_limits<int16_t>::max()));
        }

        constexpr uint16_t effectiveChargeCurrent(const CodecData &data)
        {
            return data.commonSafe && data.chargeAllowed ? data.chargeCurrentDeciA : 0U;
        }

        constexpr uint16_t effectiveDischargeCurrent(const CodecData &data)
        {
            return data.commonSafe && data.dischargeAllowed ? data.dischargeCurrentDeciA : 0U;
        }

        constexpr uint16_t candidateAAlarms(const CodecData &data)
        {
            uint16_t alarms = 0U;

            if (data.cellOverVoltage) alarms |= 1U << 0U;
            if (data.cellUnderVoltage) alarms |= 1U << 1U;
            if (data.overTemperature) alarms |= 1U << 2U;
            if (data.underTemperature) alarms |= 1U << 3U;
            if (data.overCurrent) alarms |= (1U << 4U) | (1U << 5U);
            if (data.internalFault || !data.commonSafe) alarms |= 1U << 7U;
            if (data.communicationFault) alarms |= 1U << 12U;
            if (data.cellOverVoltage) alarms |= 1U << 15U;
            if (data.cellUnderVoltage) alarms |= 1U << 14U;
            if (data.overTemperature) alarms |= 1U << 13U;
            return alarms;
        }

        constexpr uint16_t candidateAWarnings(const CodecData &data)
        {
            uint16_t warnings = 0U;
            if (data.cellOverVoltage) warnings |= 1U << 0U;
            if (data.cellUnderVoltage) warnings |= 1U << 1U;
            if (data.overTemperature) warnings |= 1U << 2U;
            if (data.underTemperature) warnings |= (1U << 3U) | (1U << 9U);
            if (data.overCurrent) warnings |= (1U << 4U) | (1U << 5U);
            if (data.communicationFault) warnings |= 1U << 6U;
            if (data.internalFault) warnings |= 1U << 11U;
            return warnings;
        }
    }

    constexpr bool encodeCandidateA(uint32_t id, const CodecData &data, EncodedFrame &frame)
    {
        frame = {};
        frame.id = id;
        frame.length = 8U;

        switch (id)
        {
        case 0x453U:
            detail::putLe16(frame.data, 0U, data.moduleCount);
            return true;

        case 0x455U:
            detail::putLe16(frame.data, 0U, detail::candidateAAlarms(data));
            detail::putLe16(frame.data, 2U, detail::candidateAWarnings(data));
            return true;

        case 0x456U:
            detail::putLe16(frame.data, 0U, data.chargeVoltageDeciV);
            detail::putLe16(frame.data, 2U, detail::effectiveChargeCurrent(data));
            detail::putLe16(frame.data, 4U,
                            static_cast<uint16_t>(-static_cast<int32_t>(
                                detail::effectiveDischargeCurrent(data))));
            detail::putLe16(frame.data, 6U, data.dischargeVoltageDeciV);
            return true;

        case 0x457U:
            detail::putLe16(frame.data, 0U,
                            detail::clampUnsigned16(static_cast<uint32_t>(data.socPercent) * 100U));
            detail::putLe16(frame.data, 2U,
                            detail::clampUnsigned16(static_cast<uint32_t>(data.sohPercent) * 100U));
            return true;

        case 0x458U:
            detail::putLe16(frame.data, 0U, data.packVoltageDeciV);
            detail::putLe16(frame.data, 2U, static_cast<uint16_t>(data.packCurrentDeciA));
            detail::putLe16(frame.data, 4U, static_cast<uint16_t>(data.averageTemperatureDeciC));
            return true;

        case 0x45AU:
#if GOODWE_CAN_A_ENABLE_45A
            return true;
#else
            return false;
#endif

        case 0x460U:
#if GOODWE_CAN_A_ENABLE_460
            frame.length = 2U;
            frame.data[0] = static_cast<uint8_t>((data.chargeAllowed ? 1U : 0U) |
                                                  (data.dischargeAllowed ? 2U : 0U));
            return true;
#else
            return false;
#endif

        default:
            return false;
        }
    }

    constexpr bool encodeCandidateB(uint32_t id, const CodecData &data, EncodedFrame &frame)
    {
        frame = {};
        frame.id = id;
        frame.length = 8U;

        switch (id)
        {
        case 0x351U:
            detail::putLe16(frame.data, 0U, data.chargeVoltageDeciV);
            detail::putLe16(frame.data, 2U, detail::effectiveChargeCurrent(data));
            detail::putLe16(frame.data, 4U, detail::effectiveDischargeCurrent(data));
            detail::putLe16(frame.data, 6U, data.dischargeVoltageDeciV);
            return true;

        case 0x355U:
            detail::putLe16(frame.data, 0U, data.socPercent);
            detail::putLe16(frame.data, 2U, data.sohPercent);
            return true;

        case 0x356U:
            detail::putLe16(frame.data, 0U,
                            detail::clampUnsigned16(static_cast<uint32_t>(data.packVoltageDeciV) * 10U));
            detail::putLe16(frame.data, 2U,
                            static_cast<uint16_t>(detail::clampSigned16(
                                -static_cast<int32_t>(data.packCurrentDeciA))));
            detail::putLe16(frame.data, 4U, static_cast<uint16_t>(data.averageTemperatureDeciC));
            return true;

        case 0x359U:
            if (data.cellOverVoltage) frame.data[0] |= 1U << 1U;
            if (data.cellUnderVoltage) frame.data[0] |= 1U << 2U;
            if (data.overTemperature) frame.data[0] |= 1U << 3U;
            if (data.underTemperature) frame.data[0] |= 1U << 4U;
            if (data.overCurrent) frame.data[0] |= 1U << 7U;
            if (data.overCurrent) frame.data[1] |= 1U << 0U;
            if (data.internalFault || !data.commonSafe) frame.data[1] |= 1U << 3U;
            if (data.cellOverVoltage) frame.data[2] |= 1U << 1U;
            if (data.cellUnderVoltage) frame.data[2] |= 1U << 2U;
            if (data.overTemperature) frame.data[2] |= 1U << 3U;
            if (data.underTemperature) frame.data[2] |= 1U << 4U;
            if (data.overCurrent) frame.data[2] |= 1U << 7U;
            if (data.overCurrent) frame.data[3] |= 1U << 0U;
            if (data.internalFault || !data.commonSafe) frame.data[3] |= 1U << 3U;
            frame.data[4] = 0x01U;
            frame.data[5] = 0x50U;
            frame.data[6] = 0x4EU;
            return true;

        default:
            return false;
        }
    }
}

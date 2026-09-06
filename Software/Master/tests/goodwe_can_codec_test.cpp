#include "Peripherals/GoodweCanCodec.h"

#include <cstdint>

namespace
{
    constexpr GoodweCan::CodecData sample{
        .commonSafe = true,
        .chargeAllowed = true,
        .dischargeAllowed = true,
        .moduleCount = 6U,
        .socPercent = 78U,
        .sohPercent = 100U,
        .chargeVoltageDeciV = 3456U,
        .dischargeVoltageDeciV = 2400U,
        .chargeCurrentDeciA = 123U,
        .dischargeCurrentDeciA = 100U,
        .packVoltageDeciV = 3072U,
        .packCurrentDeciA = 50,
        .averageTemperatureDeciC = 250,
    };

    constexpr bool candidateAEncodingIsStable()
    {
        GoodweCan::EncodedFrame frame{};

        if (!GoodweCan::encodeCandidateA(0x453U, sample, frame) ||
            frame.data[0] != 0x06U || frame.data[1] != 0x00U)
        {
            return false;
        }

        if (!GoodweCan::encodeCandidateA(0x456U, sample, frame) ||
            frame.data[0] != 0x80U || frame.data[1] != 0x0DU ||
            frame.data[2] != 0x7BU || frame.data[3] != 0x00U ||
            frame.data[4] != 0x64U || frame.data[5] != 0x00U ||
            frame.data[6] != 0x60U || frame.data[7] != 0x09U)
        {
            return false;
        }

        if (!GoodweCan::encodeCandidateA(0x457U, sample, frame) ||
            frame.data[0] != 0x78U || frame.data[1] != 0x1EU ||
            frame.data[2] != 0x10U || frame.data[3] != 0x27U)
        {
            return false;
        }

        return GoodweCan::encodeCandidateA(0x458U, sample, frame) &&
               frame.data[0] == 0x00U && frame.data[1] == 0x0CU &&
               frame.data[2] == 0x32U && frame.data[3] == 0x00U &&
               frame.data[4] == 0xFAU && frame.data[5] == 0x00U;
    }

    constexpr bool candidateBEncodingIsStable()
    {
        GoodweCan::EncodedFrame frame{};

        if (!GoodweCan::encodeCandidateB(0x351U, sample, frame) ||
            frame.data[0] != 0x80U || frame.data[1] != 0x0DU ||
            frame.data[2] != 0x7BU || frame.data[3] != 0x00U ||
            frame.data[4] != 0x64U || frame.data[5] != 0x00U ||
            frame.data[6] != 0x60U || frame.data[7] != 0x09U)
        {
            return false;
        }

        if (!GoodweCan::encodeCandidateB(0x355U, sample, frame) ||
            frame.data[0] != 0x4EU || frame.data[1] != 0x00U ||
            frame.data[2] != 0x64U || frame.data[3] != 0x00U)
        {
            return false;
        }

        if (!GoodweCan::encodeCandidateB(0x356U, sample, frame) ||
            frame.data[0] != 0x00U || frame.data[1] != 0x78U ||
            frame.data[2] != 0xCEU || frame.data[3] != 0xFFU ||
            frame.data[4] != 0xFAU || frame.data[5] != 0x00U)
        {
            return false;
        }

        return GoodweCan::encodeCandidateB(0x359U, sample, frame) &&
               frame.data[4] == 0x01U && frame.data[5] == 0x50U &&
               frame.data[6] == 0x4EU;
    }
}

static_assert(candidateAEncodingIsStable());
static_assert(candidateBEncodingIsStable());

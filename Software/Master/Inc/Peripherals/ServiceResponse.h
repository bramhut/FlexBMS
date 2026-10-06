#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ServiceResponse
{
    constexpr size_t ConfigurationDataBytes = 20U;
    constexpr size_t HeaderBytes = 2U;

    // Return zero for invalid input, otherwise the complete payload length.
    template<size_t N>
    size_t encode(std::array<uint8_t, N> &payload, uint8_t service,
                  uint8_t result, const uint8_t *data, size_t dataLength)
    {
        static_assert(N >= HeaderBytes);
        if (dataLength > N - HeaderBytes || (dataLength != 0U && data == nullptr)) return 0U;
        payload[0] = service;
        payload[1] = result;
        if (dataLength != 0U) std::memcpy(payload.data() + HeaderBytes, data, dataLength);
        return HeaderBytes + dataLength;
    }
}

#include "Peripherals/ServiceResponse.h"
#include <cassert>

int main()
{
    std::array<uint8_t, ServiceResponse::ConfigurationDataBytes> configuration{};
    for (size_t i = 0; i < configuration.size(); ++i) configuration[i] = static_cast<uint8_t>(i + 1);
    std::array<uint8_t, ServiceResponse::HeaderBytes + ServiceResponse::ConfigurationDataBytes> payload{};
    assert(ServiceResponse::encode(payload, 0x0b, 0, configuration.data(), configuration.size()) == 22);
    assert(payload[0] == 0x0b && payload[1] == 0);
    for (size_t i = 0; i < configuration.size(); ++i) assert(payload[i + 2] == configuration[i]);
    // Reproduce the original rejection and exercise the bounds/null guards.
    std::array<uint8_t, 20> oldBuffer{};
    assert(ServiceResponse::encode(oldBuffer, 0x0b, 0, configuration.data(), configuration.size()) == 0);
    assert(ServiceResponse::encode(payload, 1, 0, configuration.data(), 21) == 0);
    assert(ServiceResponse::encode(payload, 1, 0, nullptr, 1) == 0);
    assert(ServiceResponse::encode(payload, 1, 0, nullptr, 0) == 2);
    assert(ServiceResponse::encode(payload, 1, 0, configuration.data(), 4) == 6);
    assert(ServiceResponse::encode(payload, 1, 0, configuration.data(), 18) == 20);
}

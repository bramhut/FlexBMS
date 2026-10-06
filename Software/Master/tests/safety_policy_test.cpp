#include "Peripherals/SafetyPolicy.h"
#include "Peripherals/ServiceResponse.h"
#include <cassert>
int main() {
    SafetyPolicy::Values values{};
    values[SafetyPolicy::capacity_mah] = 314000;
    values[SafetyPolicy::bal_min_ma] = -100;
    values[SafetyPolicy::cell_uv_programmed_uv] = 2496000;
    const auto bytes = SafetyPolicy::encode(values);
    static_assert(SafetyPolicy::Count == 43 && SafetyPolicy::DataBytes == 174);
    assert(bytes[0] == 1 && bytes[1] == 43);
    const size_t negative = 2 + 4 * SafetyPolicy::bal_min_ma;
    assert(bytes[negative] == 0x9c && bytes[negative+1] == 0xff && bytes[negative+3] == 0xff);
    std::array<uint8_t, 176> response{};
    assert(ServiceResponse::encode(response, 0x0e, 0, bytes.data(), bytes.size()) == response.size());
    assert(response.back() == bytes.back());
    std::array<uint8_t, 22> previous{};
    assert(ServiceResponse::encode(previous, 0x0e, 0, bytes.data(), bytes.size()) == 0);
}

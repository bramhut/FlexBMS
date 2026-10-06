#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace SafetyPolicy {
    // Version 1 fixed-order signed integer fields; units are part of each name.
    enum Field : size_t {
        capacity_mah,
        cold_allowance_deci_c,
        charge_derate_mv,
        charge_stop_mv,
        discharge_derate_mv,
        discharge_stop_mv,
        cell_ov_mv,
        cell_uv_mv,
        cell_ot_deci_c,
        charge_max_ma,
        discharge_max_ma,
        recovery_permille_per_s,
        cold_stop_deci_c,
        cold_recover_deci_c,
        cold_t0_deci_c,
        cold_c0_permille,
        cold_t1_deci_c,
        cold_c1_permille,
        cold_t2_deci_c,
        cold_c2_permille,
        cold_t3_deci_c,
        cold_c3_permille,
        hot_start_deci_c,
        hot_stop_deci_c,
        hot_recover_deci_c,
        hot_c_permille,
        zero_tolerance_ma,
        limit_margin_ma,
        limit_margin_permille,
        violation_dwell_ms,
        bal_min_mv,
        bal_start_delta_mv,
        bal_stop_delta_mv,
        bal_min_ma,
        bal_max_c_permille,
        bal_qualify_ms,
        bal_pulse_ms,
        bal_max_cells,
        communication_timeout_ms,
        firmware_version,
        configuration_valid,
        cell_ov_programmed_uv,
        cell_uv_programmed_uv,
        Count
    };
    using Values = std::array<int32_t, Count>;
    inline constexpr size_t DataBytes = 2 + Count * 4;
    static_assert(DataBytes < 254);
    inline std::array<uint8_t, DataBytes> encode(const Values &values) {
        std::array<uint8_t, DataBytes> bytes{};
        bytes[0] = 1; bytes[1] = Count;
        for (size_t i=0; i<Count; ++i)
            for (size_t j=0; j<4; ++j)
                bytes[2+i*4+j] = static_cast<uint8_t>(static_cast<uint32_t>(values[i]) >> (j*8));
        return bytes;
    }
}

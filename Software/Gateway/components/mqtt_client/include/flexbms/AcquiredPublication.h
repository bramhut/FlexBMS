#pragma once

#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace FlexBms::Mqtt
{
    // UART receive and tick own this state. MQTT lifecycle callbacks only request
    // a reset; they must never publish a cached sample as a new acquisition.
    class AcquiredPublication
    {
    public:
        static constexpr int64_t kMaximumAgeUs = 3'000'000LL;

        void received(int64_t now) { acquiredAtUs = now; acquired = true; pending = true; }
        void reset() { acquired = false; pending = false; }
        bool fresh(int64_t now) const
        {
            return acquired && now >= acquiredAtUs && now - acquiredAtUs < kMaximumAgeUs;
        }
        bool due(int64_t now) const { return pending && fresh(now); }
        void published() { pending = false; }

    private:
        int64_t acquiredAtUs = 0;
        bool acquired = false;
        bool pending = false;
    };

    enum class StateSource { Diagnostic, Status, Pack, Energy };

    inline StateSource componentSource(const char *key)
    {
        for (const char *name : {"run_request", "bms_state", "hv_state", "telemetry_fresh",
                                 "hv_running", "fault_active", "active_faults", "stm32_uptime"})
            if (std::strcmp(key, name) == 0) return StateSource::Status;
        for (const char *name : {"pack_voltage", "pack_current", "pack_power", "state_of_charge",
                                 "min_cell_voltage", "max_cell_voltage", "cell_delta",
                                 "min_ntc_temperature", "max_ntc_temperature"})
            if (std::strcmp(key, name) == 0) return StateSource::Pack;
        for (const char *name : {"battery_energy_charged", "battery_energy_discharged"})
            if (std::strcmp(key, name) == 0) return StateSource::Energy;
        return StateSource::Diagnostic;
    }
}

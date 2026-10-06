#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace FlexBms::EnergyMeters
{
    constexpr size_t kMeterCount = 2U;
    constexpr size_t kNameBytes = 48U;
    constexpr int64_t kElectricalPeriodUs = 1'000'000;
    constexpr int64_t kEnergyPeriodUs = 30'000'000;
    constexpr int64_t kElectricalFreshUs = 3'000'000;
    constexpr int64_t kEnergyFreshUs = 90'000'000;

    enum class Parity : uint8_t { None, Even, Odd };
    struct MeterConfiguration
    {
        bool enabled = false;
        std::array<char, kNameBytes + 1U> name{};
        uint8_t address = 1U;
        bool reversePowerDirection = false;
    };
    struct Configuration
    {
        uint32_t baudRate = 9600U;
        Parity parity = Parity::None;
        uint8_t stopBits = 1U;
        std::array<MeterConfiguration, kMeterCount> meters{};
    };
    inline Configuration defaults()
    {
        Configuration config{};
        for (size_t index = 0; index < kMeterCount; ++index)
        {
            config.meters[index].address = static_cast<uint8_t>(index + 1U);
            std::snprintf(config.meters[index].name.data(), config.meters[index].name.size(), "Meter %u", static_cast<unsigned>(index + 1U));
        }
        return config;
    }
    inline bool anyEnabled(const Configuration &config) { return config.meters[0].enabled || config.meters[1].enabled; }
    inline bool valid(const Configuration &config)
    {
        if (config.baudRate != 1200U && config.baudRate != 2400U && config.baudRate != 4800U && config.baudRate != 9600U && config.baudRate != 19200U) return false;
        if (config.parity > Parity::Odd || (config.stopBits != 1U && config.stopBits != 2U)) return false;
        for (const auto &meter : config.meters)
        {
            if (meter.address == 0U || meter.address > 247U || meter.name.back() != '\0') return false;
            if (meter.enabled && meter.name[0] == '\0') return false;
            for (const char *p = meter.name.data(); *p; ++p)
                if (static_cast<unsigned char>(*p) < 32U || static_cast<unsigned char>(*p) == 127U) return false;
        }
        return !config.meters[0].enabled || !config.meters[1].enabled || config.meters[0].address != config.meters[1].address;
    }

    struct Reading
    {
        std::array<float, 3> currentA{};
        std::array<float, 3> powerW{};
        float totalPowerW = 0;
        float importKwh = 0;
        float exportKwh = 0;
        bool electricalValid = false;
        bool energyValid = false;
        int64_t electricalUs = 0;
        int64_t energyUs = 0;
        uint32_t failedReads = 0;
        int32_t lastError = 0;
        uint32_t serialNumber = 0;
        uint16_t meterCode = 0;
        uint16_t firmwareVersion = 0;
        bool identityValid = false;
        bool firmwareValid = false;
    };
    struct State
    {
        Configuration configuration = defaults();
        std::array<Reading, kMeterCount> readings{};
        uint32_t generation = 1;
        bool available = false;
        bool busReady = false;
        bool storedConfigurationValid = true;
        int32_t busError = 0;
    };
    inline bool fresh(bool hasValue, int64_t sampledUs, int64_t nowUs, int64_t lifetimeUs)
    {
        return hasValue && nowUs >= sampledUs && nowUs - sampledUs < lifetimeUs;
    }
    // ESP-Modbus's raw read callback provides host-endian 16-bit registers.
    // Eastron floats are high-word first; never reinterpret uint16_t arrays.
    inline float decodeFloat(const uint16_t *registers)
    {
        const uint32_t bits = (static_cast<uint32_t>(registers[0]) << 16U) | registers[1];
        float value = 0;
        static_assert(sizeof(value) == sizeof(bits));
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    inline bool decodeElectrical(const uint16_t *phases, const uint16_t *total, Reading &reading)
    {
        Reading candidate = reading;
        for (size_t phase = 0; phase < 3U; ++phase)
        {
            candidate.currentA[phase] = decodeFloat(phases + phase * 2U);
            candidate.powerW[phase] = decodeFloat(phases + 6U + phase * 2U);
            if (!std::isfinite(candidate.currentA[phase]) || candidate.currentA[phase] < 0 || candidate.currentA[phase] > 120 ||
                !std::isfinite(candidate.powerW[phase]) || std::fabs(candidate.powerW[phase]) > 100'000) return false;
        }
        candidate.totalPowerW = decodeFloat(total);
        if (!std::isfinite(candidate.totalPowerW) || std::fabs(candidate.totalPowerW) > 300'000) return false;
        reading = candidate;
        return true;
    }
    inline bool decodeEnergy(const uint16_t *registers, Reading &reading)
    {
        const float imported = decodeFloat(registers), exported = decodeFloat(registers + 2U);
        if (!std::isfinite(imported) || !std::isfinite(exported) || imported < 0 || exported < 0) return false;
        reading.importKwh = imported;
        reading.exportKwh = exported;
        return true;
    }
    inline Reading reported(Reading reading, bool reverse)
    {
        if (reverse)
        {
            for (auto &power : reading.powerW) power = -power;
            reading.totalPowerW = -reading.totalPowerW;
            const float imported = reading.importKwh;
            reading.importKwh = reading.exportKwh;
            reading.exportKwh = imported;
        }
        return reading;
    }
}

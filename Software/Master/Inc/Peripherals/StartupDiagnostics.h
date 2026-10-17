#pragma once
#include <cstdint>

namespace StartupDiagnostics
{
    constexpr uint8_t temperatureInputMask(uint8_t count)
    {
        return static_cast<uint8_t>((1U << (count < 7U ? count : 7U)) - 1U);
    }

    constexpr bool hasOpenTemperatureInput(uint16_t openMask, uint8_t count)
    {
        return (openMask & temperatureInputMask(count)) != 0U;
    }

    // Values are part of GET_DIAGNOSTIC_REPORT's wire format.
    enum class RegisterOperation : uint8_t
    {
        None = 0U,
        ReadFailed = 1U,
        WriteFailed = 2U,
        ReadCompleted = 3U,
        WriteCompleted = 4U,
    };

    struct RegisterTrace
    {
        uint8_t address{0xFFU};
        RegisterOperation operation{RegisterOperation::None};
        uint16_t value{};
    };

    struct Report
    {
        uint8_t cid{};
        uint16_t failedChecks{};
        uint8_t status{};
        uint8_t failedDiagnostic{0xFFU};
        RegisterTrace trace{};
        uint16_t attempt{};
        uint8_t gpioOpenMask{};
        uint8_t gpioCheckedMask{};

        constexpr bool failed() const { return failedChecks != 0U || status != 0U; }
    };

    // A later communication failure must not erase the original evidence.
    constexpr void retainFirstFailure(Report &stored, const Report &next)
    {
        if (!stored.failed()) stored = next;
    }
}

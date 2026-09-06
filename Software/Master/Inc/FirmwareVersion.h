#pragma once

#include <cstdint>

// Written by scripts/build-release.ps1 for every STM32 release image.
inline constexpr uint8_t FIRMWARE_VERSION_MAJOR = 0U;
inline constexpr uint8_t FIRMWARE_VERSION_MINOR = 1U;
inline constexpr uint8_t FIRMWARE_VERSION_PATCH = 44U;
inline constexpr uint8_t FIRMWARE_VERSION_BUILD = 0U;

inline constexpr uint32_t FIRMWARE_VERSION_PACKED =
    static_cast<uint32_t>(FIRMWARE_VERSION_MAJOR) |
    (static_cast<uint32_t>(FIRMWARE_VERSION_MINOR) << 8U) |
    (static_cast<uint32_t>(FIRMWARE_VERSION_PATCH) << 16U) |
    (static_cast<uint32_t>(FIRMWARE_VERSION_BUILD) << 24U);

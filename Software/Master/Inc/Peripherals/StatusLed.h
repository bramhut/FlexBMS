#pragma once

namespace StatusLed
{
    // Commissioning may tune these independently. BoardIO applies the active-low
    // PWM polarity; values here are logical on-level caps.
    constexpr double kRedBrightness = 0.35;
    constexpr double kGreenBrightness = 0.30;

    void setup();
    void update();

    // Reserved integration hooks for local failures which do not yet have a
    // dedicated firmware source.
    void setFirmwareUpdateActive(bool active);
    void setFatalLocalFailure(bool active);
}

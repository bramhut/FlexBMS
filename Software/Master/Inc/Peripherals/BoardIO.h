#pragma once

#include "HelperFunc.h"
#include <cstdint>

namespace IO
{
    void setup();

    void setLEDcolor(RGB_t color);
    void setLEDcolor(HSV_t color);
    void setLED(bool state);

    void setPrechargeRelay(bool enabled);
    void setContactorDutyPercent(uint8_t dutyPercent);
    // Does not depend on the scheduler and is safe to call from a fatal handler.
    void emergencySafeOff();

    bool areHVSensorDiagnosticsHealthy();

    // Performs the non-intrusive STM32 ADC health check and reports ADC_FAULT
    // after its debounce period.
    void updateAdcHealth();
    bool isUsbPresent();

    double getLoadSideVoltage();
    double getBatterySideVoltage();
}

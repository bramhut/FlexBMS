#pragma once

#include "HelperFunc.h"
#include <cstdint>

namespace IO
{
    struct HVVoltages
    {
        bool valid = false;
        double batteryVoltage = 0.0;
        double loadVoltage = 0.0;
    };

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
    // Returns both AMC3330 channels from one ADC/DMA snapshot.  Invalid data
    // must not be displayed as a measured zero voltage.
    HVVoltages getHVVoltages();
}

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

    bool areHVSensorDiagnosticsHealthy();
    bool isUsbPresent();

    double getLoadSideVoltage();
    double getBatterySideVoltage();
}

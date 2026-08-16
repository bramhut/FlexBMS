#pragma once

#include <cstdint>

#ifndef HV_CONTACTOR_HOLD_DUTY_PERCENT
#define HV_CONTACTOR_HOLD_DUTY_PERCENT 50U
#endif

#if HV_CONTACTOR_HOLD_DUTY_PERCENT > 100U
#error "HV_CONTACTOR_HOLD_DUTY_PERCENT must not exceed 100"
#endif

namespace PCC
{
    enum PCC_STATE : uint8_t
    {
        OFF,
        SELF_TEST,
        PRECHARGE,
        CONTACTOR_CLOSE,
        RUN
    };

    enum PCC_ERROR : uint8_t
    {
        NO_ERROR,
        SENSOR_DIAGNOSTIC_ERROR,
        USB_ONLY_ERROR,
        BATTERY_VOLTAGE_MISMATCH,
        LOAD_SIDE_ENERGIZED,
        PRECHARGE_TIMEOUT,
        PRECHARGE_VOLTAGE_LOST,
        CONTACTOR_VOLTAGE_LOST
    };

    enum HV_ERROR_MASK : uint16_t
    {
        HV_ERROR_NONE = 0U,
        HV_ERROR_SENSOR_DIAGNOSTIC = 1U << 0,
        HV_ERROR_USB_ONLY = 1U << 1,
        HV_ERROR_BATTERY_VOLTAGE_MISMATCH = 1U << 2,
        HV_ERROR_LOAD_SIDE_ENERGIZED = 1U << 3,
        HV_ERROR_PRECHARGE_TIMEOUT = 1U << 4,
        HV_ERROR_PRECHARGE_VOLTAGE_LOST = 1U << 5,
        HV_ERROR_CONTACTOR_VOLTAGE_LOST = 1U << 6
    };

    void setup();
    void loop();

    void setRunRequest(bool requested);
    bool requestFaultClear();
    bool isSafeForFirmwareUpdate();
    bool enterFirmwareUpdateLock();
    bool isFirmwareUpdateLocked();
    bool isRunRequested();

    PCC_STATE getPCCState();
    PCC_ERROR getPCCError();
    uint32_t getLastPrechargeTime();

    uint16_t getActiveErrors();
    uint16_t getLatchedErrors();
    uint16_t getHistoricalErrors();

    double getBatteryVoltage();
    double getLoadVoltage();
}

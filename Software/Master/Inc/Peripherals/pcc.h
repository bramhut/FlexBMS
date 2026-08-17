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
        BATTERY_VOLTAGE_MISMATCH,
        LOAD_SIDE_ENERGIZED,
        PRECHARGE_TIMEOUT,
        PRECHARGE_VOLTAGE_LOST,
        CONTACTOR_VOLTAGE_LOST
    };

    void setup();
    void loop();

    void setRunRequest(bool requested);
    // FaultManager is the only caller for this immediate safe-off command.
    void forceSafeOffFromFaultManager();
    bool isSafeForFirmwareUpdate();
    bool prepareFirmwareUpdate();
    bool commitFirmwareUpdate();
    bool isFirmwareUpdatePrepared();
    bool isFirmwareUpdateLocked();
    bool isRunRequested();

    PCC_STATE getPCCState();
    PCC_ERROR getPCCError();
    uint32_t getLastPrechargeTime();

    double getBatteryVoltage();
    double getLoadVoltage();
}

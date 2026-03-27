#pragma once

#include <cstdint>
#include "bcc/bcc.h"
#include "bcc/SlaveController.h"
#include "CAN.h"
#include "HelperFunc.h"
#include "TimeFunctions.h"

namespace PCC
{

    enum PCC_STATE
    {
        STARTUP,
        IDLE,
        PRECHARGING,
        SUCCESS,
        ERROR
    };

    enum PCC_ERROR
    {
        NO_ERROR,
        TIMEOUT, // Error when the precharge time exceeds the limit
        COMMUNICATION_ERROR, // Error when no CAN messages are received within the timeout period
        TS_DEVIATION, //Error when DCDC and Compressor disagree on TS voltage
    };

    enum TSAC_LOCATION
    {
        UNKOWN,
        CAR,
        CHARGING_CART
    };

    PCC_STATE getPCCState(); // Get the current PCC state
    PCC_ERROR getPCCError(); // Get the current PCC error state

    void setFinishPrechargeCommand(bool command);

    uint32_t getLastPrechargeTime(); // Get the last precharge time in ms

    void setup(CAN *_Can);
    void loop();

};

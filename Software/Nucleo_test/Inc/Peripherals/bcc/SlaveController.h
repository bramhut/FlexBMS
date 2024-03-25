#pragma once
#include "bcc/bcc.h"

namespace SlaveController
{
    enum BMSFault
    {
        NO_FAULT,
        CID_INITIALIZATION_FAULT,
        REGISTER_INITIALIZATION_FAULT,
        ADC_CONVERSION_FAULT,
        CELL_BALANCING_FAULT,
        DIAGNOSTICS_FAULT,

    };

    void mainTask(){};

    inline BMSFault getCurrentFault() { return mCurrentFault; }
}
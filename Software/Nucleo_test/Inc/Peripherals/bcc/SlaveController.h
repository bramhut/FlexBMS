#pragma once
#include "bcc/bcc.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/


namespace SlaveController
{
    enum BMSFault
    {
        NO_FAULT,
        INVALID_USER_SETTINGS,
        INVALID_SLAVE_CONFIG,
        TPL_FAULT,
        CID_INITIALIZATION_FAULT,
        REGISTER_INITIALIZATION_FAULT,
        ADC_CONVERSION_FAULT,
        CELL_BALANCING_FAULT,
        DIAGNOSTICS_FAULT,

    };




    void setup();
}
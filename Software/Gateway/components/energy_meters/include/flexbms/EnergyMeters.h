#pragma once
#include "flexbms/MeterModel.h"

namespace FlexBms::EnergyMeters
{
    // Optional Gateway-only reads. UART0 and GPIO6/7/0 belong exclusively to
    // this worker; no BMS UART, inverter commands or meter writes are exposed.
    bool start();
    bool configure(const Configuration &configuration);
    State getState();
}

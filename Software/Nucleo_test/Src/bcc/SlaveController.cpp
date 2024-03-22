#include "bcc/SlaveController.h"
#include "Time.h"
void SlaveController::mainTask()
{
    // First attempt mirrors recommend flow according to Programming guide

    while (true) {
        // 1. Daisy chain / CID initialization
        while (!initializeDaisyChain()) {
            currentFault = CID_INITIALIZATION_FAULT;
        }
        currentFault = NO_FAULT;

        // 2. Register initialization
        initializeRegisters();

        // main loop
        runningLoop();
    } 
   
    
}

void SlaveController::runningLoop()
{

    while (true) {
        // 3. ADC conversions
        ADCConversions();

        // 4. Cell balancing
        performCellBalancing();

        // 5. Diagnostics
        diagnostics();

        // 6. CID presence check
       if (!allCIDsPresence()) {
           return;
       }
        
    }
    
}


bool SlaveController::initializeDaisyChain()
{
    int Ncid = 1;
    
    // TPL Wakeup 
    return;
}
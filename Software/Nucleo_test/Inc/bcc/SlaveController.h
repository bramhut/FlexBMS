#pragma once


enum BMSFault {
    NO_FAULT,
    CID_INITIALIZATION_FAULT,
    REGISTER_INITIALIZATION_FAULT,
    ADC_CONVERSION_FAULT,
    CELL_BALANCING_FAULT,
    DIAGNOSTICS_FAULT,

};

class SlaveController
{

    private:

        BMSFault currentFault = NO_FAULT;


        bool initializeDaisyChain();
        bool initializeRegisters();
        bool ADCConversions();
        bool performCellBalancing();
        bool diagnostics();
        bool allCIDsPresence();

        void runningLoop();
    public:
        SlaveController() {};
        void mainTask() {};

        BMSFault getCurrentFault() {
            return currentFault;
        }
};
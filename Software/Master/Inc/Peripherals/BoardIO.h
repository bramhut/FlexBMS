// This file contains the peripheral IO definitions for the BMS Master board

#pragma once
#include "main.h"
#include "HelperFunc.h"
#include <functional>

namespace IO
{

    /* GLOBALS */
    extern RGB_t _ledColors;
    extern bool _ledState;

    extern std::vector<std::function<void(bool state)>> _switchCallbacks;

    /* FUNCTIONS */

    // Setup the IO peripherals
    void setup();

    // Registers a callback function that is called when the switch is changed. This function is called from an ISR, so be careful
    void onSwitchChange(std::function<void(bool state)> callback);

    // Returns true if the switch is in the ON position
    bool getSwitch();

    // Set the color of the RGB LED
    void setLEDcolor(RGB_t color);
    void setLEDcolor(HSV_t color);

    // Set the state of the RGB LED
    void setLED(bool state);

    // Set the state of the isolated relay driver
    void setRelay(bool state);

    /* PRIVATE FUNCTIONS */
    // This function is called from the ISR to handle the switch callbacks
    void _handleCallbacks(uint16_t GPIO_Pin);

    // Sets the state of the precharge relay and the AIR relay
    // true = precharge closed and AIR open, false = precharge open and AIR closed
    void togglePrechargeAndAIR(bool state);

    // Returns true if the FSDC is enabled
    bool getFSDCState();

    // Return current from the HALL-effect sensor [A]
    double getCurrent();

}

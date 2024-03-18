
/*
 * File: bcc_wait.c
 *
 * This file implements functions for busy waiting for BCC driver. 
 * This file is not part of the driver, but contains functionality specifically for the STM32 / this project.
 */

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "bcc/bcc_wait.h"
#include "Time.h"


/*FUNCTION**********************************************************************
 *
 * Function Name : BCC_WaitSec
 * Description   : Waits for specified amount of seconds.
 *
 *END**************************************************************************/
void BCC_MCU_WaitSec(uint16_t delay)
{
    for (; delay > 0U; delay--) {
        BCC_MCU_WaitMs(1000U);
    }
}

/*FUNCTION**********************************************************************
 *
 * Function Name : BCC_WaitMs
 * Description   : Waits for specified amount of milliseconds.
 *
 *END**************************************************************************/
void BCC_MCU_WaitMs(uint16_t delay_ms)
{
    
    delay(delay_ms);

}

/*FUNCTION**********************************************************************
 *
 * Function Name : BCC_WaitUs
 * Description   : Waits for specified amount of microseconds.
 *
 *END**************************************************************************/
void BCC_MCU_WaitUs(uint32_t delay)
{
    uint32_t cycles;

    if (delay > 1000)
    {
        BCC_MCU_WaitMs(delay / 1000);
        delay %= 1000;
    }

    delayUS(delay);
}

/*******************************************************************************
 * EOF
 ******************************************************************************/

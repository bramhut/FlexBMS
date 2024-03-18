/*!
 * File: bcc_wait.h
 *
 * This file implements functions for busy waiting for BCC driver. 
 * This file is not part of the driver, but contains functionality specifically for the STM32 / this project.
 */


#ifndef BCC_WAIT_H_
#define BCC_WAIT_H_

#include "main.h"


/*! @} */

/*******************************************************************************
 * API
 ******************************************************************************/

/*!
 * @brief Waits for specified amount of seconds.
 *
 * @param delay Number of seconds to wait.
 */
void BCC_MCU_WaitSec(uint16_t delay);

/*!
 * @brief Waits for specified amount of milliseconds.
 *
 * @param delay Number of milliseconds to wait.
 */
void BCC_MCU_WaitMs(uint16_t delay);

/*!
 * @brief Waits for specified amount of microseconds.
 *
 * @param delay Number of microseconds to wait.
 */
void BCC_MCU_WaitUs(uint32_t delay);
/*! @} */

#endif /* BCC_WAIT_H_ */
/*******************************************************************************
 * EOF
 ******************************************************************************/
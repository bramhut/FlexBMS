/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32g4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "board_def.h"
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */
void SystemClock_Config(void);
/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define V_SENSE_LOAD_DIAG_Pin GPIO_PIN_13
#define V_SENSE_LOAD_DIAG_GPIO_Port GPIOC
#define LSE_OSC_IN_Pin GPIO_PIN_14
#define LSE_OSC_IN_GPIO_Port GPIOC
#define LSE_OSC_OUT_Pin GPIO_PIN_15
#define LSE_OSC_OUT_GPIO_Port GPIOC
#define HSE_OSC_IN_Pin GPIO_PIN_0
#define HSE_OSC_IN_GPIO_Port GPIOF
#define HSE_OSC_OUT_Pin GPIO_PIN_1
#define HSE_OSC_OUT_GPIO_Port GPIOF
#define V_SENSE_LOAD__Pin GPIO_PIN_0
#define V_SENSE_LOAD__GPIO_Port GPIOA
#define V_SENSE_LOAD_A1_Pin GPIO_PIN_1
#define V_SENSE_LOAD_A1_GPIO_Port GPIOA
#define V_SENSE_BAT__Pin GPIO_PIN_2
#define V_SENSE_BAT__GPIO_Port GPIOA
#define V_SENSE_BAT_A3_Pin GPIO_PIN_3
#define V_SENSE_BAT_A3_GPIO_Port GPIOA
#define SPI_TX_NSS_Pin GPIO_PIN_4
#define SPI_TX_NSS_GPIO_Port GPIOA
#define SPI_TX_SCK_Pin GPIO_PIN_5
#define SPI_TX_SCK_GPIO_Port GPIOA
#define CONT_EN_Pin GPIO_PIN_6
#define CONT_EN_GPIO_Port GPIOA
#define MC_INT_Pin GPIO_PIN_7
#define MC_INT_GPIO_Port GPIOA
#define PCC_EN_Pin GPIO_PIN_0
#define PCC_EN_GPIO_Port GPIOB
#define AUX_RELAY_EN_Pin GPIO_PIN_1
#define AUX_RELAY_EN_GPIO_Port GPIOB
#define V_SENSE_BAT_DIAG_Pin GPIO_PIN_2
#define V_SENSE_BAT_DIAG_GPIO_Port GPIOB
#define VCP_TX_Pin GPIO_PIN_10
#define VCP_TX_GPIO_Port GPIOB
#define VCP_RX_Pin GPIO_PIN_11
#define VCP_RX_GPIO_Port GPIOB
#define SPI_RX_NSS_Pin GPIO_PIN_12
#define SPI_RX_NSS_GPIO_Port GPIOB
#define SPI_RX_SCK_Pin GPIO_PIN_13
#define SPI_RX_SCK_GPIO_Port GPIOB
#define MC_EN_Pin GPIO_PIN_14
#define MC_EN_GPIO_Port GPIOB
#define SPI_RX_MOSI_Pin GPIO_PIN_15
#define SPI_RX_MOSI_GPIO_Port GPIOB
#define LED_R_Pin GPIO_PIN_8
#define LED_R_GPIO_Port GPIOA
#define LED_Y_UART_TX_ESP_Pin GPIO_PIN_9
#define LED_Y_UART_TX_ESP_GPIO_Port GPIOA
#define UART_RX_ESP_Pin GPIO_PIN_10
#define UART_RX_ESP_GPIO_Port GPIOA
#define USB_N_Pin GPIO_PIN_11
#define USB_N_GPIO_Port GPIOA
#define USB_P_Pin GPIO_PIN_12
#define USB_P_GPIO_Port GPIOA
#define SWDIO_Pin GPIO_PIN_13
#define SWDIO_GPIO_Port GPIOA
#define SWCLK_Pin GPIO_PIN_14
#define SWCLK_GPIO_Port GPIOA
#define AUX_IO2_Pin GPIO_PIN_15
#define AUX_IO2_GPIO_Port GPIOA
#define SWO_Pin GPIO_PIN_3
#define SWO_GPIO_Port GPIOB
#define V_SENSE_USB_Pin GPIO_PIN_4
#define V_SENSE_USB_GPIO_Port GPIOB
#define SPI_TX_MOSI_Pin GPIO_PIN_5
#define SPI_TX_MOSI_GPIO_Port GPIOB
#define LED_G_Pin GPIO_PIN_6
#define LED_G_GPIO_Port GPIOB
#define AUX_IO1_Pin GPIO_PIN_7
#define AUX_IO1_GPIO_Port GPIOB
#define CAN_RX_Pin GPIO_PIN_8
#define CAN_RX_GPIO_Port GPIOB
#define CAN_TX_Pin GPIO_PIN_9
#define CAN_TX_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */

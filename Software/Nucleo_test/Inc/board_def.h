// This file defines board-specific features and peripherals

/***** SETTINGS *****/

#define BOARD_SERIES_G4
// #define BOARD_SERIES_H7


// Timer peripherals
#define MICROS_TIMER htim7


// ADC peripherals





/***** DON'T EDIT *****/

#define ADC_ENABLED __has_include("adc.h")
#define DAC_ENABLED __has_include("dac.h")
#define CAN_ENABLED __has_include("fdcan.h")
#define USB_ENABLED __has_include("usb_device.h")
#define I2C_ENABLED __has_include("i2c.h")
#define USART_ENABLED __has_include("usart.h")
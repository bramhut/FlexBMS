// This file defines board-specific features and peripherals

/***** SETTINGS *****/

#define BOARD_SERIES_G4
// #define BOARD_SERIES_H7


// Timer peripherals
#define MICROS_TIMER htim7


/* ADC peripherals */
#define ADC1_ENABLED
#define ADC1_CHANNELS 4
// #define ADC2_ENABLED
// #define ADC2_CHANNELS 4
// #define ADC3_ENABLED
// #define ADC3_CHANNELS 4

// Use ratiometric mode or absolute voltage mode
#define RATIOMETRIC 




/***** DON'T EDIT *****/

#define ADC_ENABLED __has_include("adc.h")
#define DAC_ENABLED __has_include("dac.h")
#define CAN_ENABLED __has_include("fdcan.h")
#define USB_ENABLED __has_include("usb_device.h")
#define I2C_ENABLED __has_include("i2c.h")
#define USART_ENABLED __has_include("usart.h")
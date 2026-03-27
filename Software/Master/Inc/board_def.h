// This file defines board-specific features and peripherals

/***** SETTINGS *****/

/* Peripheral - Timers */

    // Timer peripherals
    #define MICROS_TIMER htim6 // Share this timer with the Timebase source timer as set in CubeMX (NOT systick)

/* Peripheral - ADC */

    // Number of ADC channels, 0 = disabled
    #define ADC1_CHANNELS 4
    #define ADC2_CHANNELS 1
    #define ADC3_CHANNELS 0

    // Define the channel for VREFINT. Start counting from channel 0, adc1
    // So if VREFINT is on channel 3, adc3: set to ADC1_CHANNELS + ADC2_CHANNELS + 3
    #define ADC_VREFINT_IDX 2

/* Peripheral - USB */

    // USB memory allocation
    #define USB_TX_TASK_STACK_SIZE 128 * 8
    #define USB_TX_BUF_SIZE 2048
    #define USB_RX_BUF_SIZE 2048

/* Periperal - BCC */

    /*! @brief HAL SPI handlers. */
    #define BCC_TX_HSPI hspi1
    #define BCC_RX_HSPI hspi2

    /*! @brief SPI TX CS pins. */
    #define BCC_SPI_TX_CS_PORT SPI_TX_NSS_GPIO_Port
    #define BCC_SPI_TX_CS_PIN SPI_TX_NSS_Pin

    /*! @brief EN and INT pins. */
    #define BCC_EN_PORT MC_EN_GPIO_Port
    #define BCC_EN_PIN MC_EN_Pin
    #define BCC_INT_PORT MC_INT_GPIO_Port
    #define BCC_INT_PIN MC_INT_Pin

    #define WSEN_TIDS_MODULES

/***** DON'T EDIT *****/

    #define ADC_ENABLED __has_include("adc.h")
    #define DAC_ENABLED __has_include("dac.h")
    #define CAN_ENABLED __has_include("fdcan.h")
    #define USB_ENABLED __has_include("usb_device.h")
    #define I2C_ENABLED __has_include("i2c.h")
    #define USART_ENABLED __has_include("usart.h")
    #define SPI_ENABLED __has_include("spi.h")
    #define FREERTOS_ENABLED __has_include("FreeRTOS.h")
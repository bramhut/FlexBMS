#include "BoardIO.h"

#include "adc.h"
#include "main.h"
#include "tim.h"

#include <algorithm>

#define DEBUG_LVL 2
#include "Debug.h"

namespace IO
{
    namespace
    {
        constexpr size_t ADC_CHANNEL_COUNT = 5U;
        constexpr size_t LOAD_VOLTAGE_INDEX = 0U;
        constexpr size_t BATTERY_VOLTAGE_INDEX = 1U;
        constexpr size_t VREFINT_INDEX = 3U;
        constexpr double ADC_OVERSAMPLING_GAIN = 16.0;
        constexpr double ADC_DIFFERENTIAL_SCALE = 2048.0 * ADC_OVERSAMPLING_GAIN;
        constexpr double HV_DIVIDER_AND_GAIN = 801.0 / 2.0;

        alignas(4) volatile uint32_t adcValues[ADC_CHANNEL_COUNT] = {};
        RGB_t ledColor{1.0, 1.0, 1.0};
        bool ledState = false;

        double getVdda()
        {
            const uint32_t vrefRaw = adcValues[VREFINT_INDEX];
            if (vrefRaw == 0U)
            {
                return 0.0;
            }

            return (static_cast<double>(VREFINT_CAL_VREF) / 1000.0) *
                   static_cast<double>(*VREFINT_CAL_ADDR) *
                   ADC_OVERSAMPLING_GAIN /
                   static_cast<double>(vrefRaw);
        }

        double getHVVoltage(size_t index)
        {
            const double vdda = getVdda();
            const int16_t differentialRaw =
                static_cast<int16_t>(adcValues[index] & 0xFFFFU);
            const double adcDifferentialVoltage =
                static_cast<double>(differentialRaw) * vdda /
                ADC_DIFFERENTIAL_SCALE;
            return std::max(0.0, adcDifferentialVoltage * HV_DIVIDER_AND_GAIN);
        }
    }

    void setup()
    {
        setPrechargeRelay(false);
        setContactorDutyPercent(0U);
        setLED(false);

        if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK ||
            HAL_ADCEx_Calibration_Start(&hadc1, ADC_DIFFERENTIAL_ENDED) != HAL_OK ||
            HAL_ADC_Start_DMA(
                &hadc1,
                const_cast<uint32_t *>(adcValues),
                ADC_CHANNEL_COUNT) != HAL_OK)
        {
            PRINTF_ERR("[IO] ADC1 calibration or DMA startup failed\n");
        }
    }

    void setLEDcolor(RGB_t color)
    {
        ledColor = color;
        setLED(ledState);
    }

    void setLEDcolor(HSV_t color)
    {
        setLEDcolor(hsv2rgb(color));
    }

    void setLED(bool state)
    {
        ledState = state;
        const double red = state ? ledColor.r : 0.0;
        const double green = state ? ledColor.g : 0.0;
        __HAL_TIM_SET_COMPARE(
            &htim1,
            TIM_CHANNEL_1,
            static_cast<uint32_t>(std::clamp(red, 0.0, 1.0) * 254.0));
        __HAL_TIM_SET_COMPARE(
            &htim4,
            TIM_CHANNEL_1,
            static_cast<uint32_t>(std::clamp(green, 0.0, 1.0) * (254.0 / 3.0)));

        if (state)
        {
            HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
            HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_1);
        }
        else
        {
            HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
            HAL_TIM_PWM_Stop(&htim4, TIM_CHANNEL_1);
        }
    }

    void setPrechargeRelay(bool enabled)
    {
        HAL_GPIO_WritePin(
            PCC_EN_GPIO_Port,
            PCC_EN_Pin,
            enabled ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }

    void setContactorDutyPercent(uint8_t dutyPercent)
    {
        const uint32_t boundedDuty = std::min<uint32_t>(dutyPercent, 100U);
        const uint32_t periodCounts = __HAL_TIM_GET_AUTORELOAD(&htim3) + 1U;
        const uint32_t pulseCounts = periodCounts * boundedDuty / 100U;

        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, pulseCounts);
        if (boundedDuty == 0U)
        {
            HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_1);
        }
        else
        {
            HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
        }
    }

    bool areHVSensorDiagnosticsHealthy()
    {
        return HAL_GPIO_ReadPin(
                   V_SENSE_LOAD_DIAG_GPIO_Port,
                   V_SENSE_LOAD_DIAG_Pin) == GPIO_PIN_SET &&
               HAL_GPIO_ReadPin(
                   V_SENSE_BAT_DIAG_GPIO_Port,
                   V_SENSE_BAT_DIAG_Pin) == GPIO_PIN_SET;
    }

    bool isUsbPresent()
    {
        return HAL_GPIO_ReadPin(
                   V_SENSE_USB_GPIO_Port,
                   V_SENSE_USB_Pin) == GPIO_PIN_SET;
    }

    double getLoadSideVoltage()
    {
        return getHVVoltage(LOAD_VOLTAGE_INDEX);
    }

    double getBatterySideVoltage()
    {
        return getHVVoltage(BATTERY_VOLTAGE_INDEX);
    }
}

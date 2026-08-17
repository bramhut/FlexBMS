#include "BoardIO.h"

#include "adc.h"
#include "FaultManager.h"
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
        constexpr uint32_t ADC_HEALTH_CHECK_PERIOD_MS = 100U;
        constexpr uint8_t ADC_HEALTH_FAILURE_LIMIT = 3U;
        constexpr double MIN_VALID_VDDA = 2.7;
        constexpr double MAX_VALID_VDDA = 3.6;

        alignas(4) volatile uint32_t adcValues[ADC_CHANNEL_COUNT] = {};
        volatile uint32_t adcDmaCompletionCount = 0U;
        bool adcStarted = false;
        uint32_t lastAdcDmaCompletionCount = 0U;
        uint32_t lastAdcHealthCheckAt = 0U;
        uint8_t adcHealthFailureCount = 0U;
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

        bool isAdcHealthy()
        {
            const uint32_t currentDmaCompletionCount = adcDmaCompletionCount;
            const bool dmaProgressed =
                currentDmaCompletionCount != lastAdcDmaCompletionCount;
            lastAdcDmaCompletionCount = currentDmaCompletionCount;

            const double vdda = getVdda();
            return adcStarted && dmaProgressed &&
                   HAL_ADC_GetError(&hadc1) == HAL_ADC_ERROR_NONE &&
                   hadc1.DMA_Handle != nullptr &&
                   HAL_DMA_GetError(hadc1.DMA_Handle) == HAL_DMA_ERROR_NONE &&
                   vdda >= MIN_VALID_VDDA && vdda <= MAX_VALID_VDDA;
        }
    }

    void reportAdcDmaCompletion(ADC_HandleTypeDef *hadc)
    {
        if (hadc->Instance == ADC1)
        {
            ++adcDmaCompletionCount;
        }
    }

    void setup()
    {
        setPrechargeRelay(false);
        setContactorDutyPercent(0U);
        setLED(false);

        adcStarted = HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) == HAL_OK &&
                     HAL_ADCEx_Calibration_Start(&hadc1, ADC_DIFFERENTIAL_ENDED) == HAL_OK &&
                     HAL_ADC_Start_DMA(
                         &hadc1,
                         const_cast<uint32_t *>(adcValues),
                         ADC_CHANNEL_COUNT) == HAL_OK;
        if (!adcStarted)
        {
            PRINTF_ERR("[IO] ADC1 calibration or DMA startup failed\n");
            FaultManager::setBmsFault(FaultManager::BmsFault::AdcFault, true);
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

    void emergencySafeOff()
    {
        HAL_GPIO_WritePin(PCC_EN_GPIO_Port, PCC_EN_Pin, GPIO_PIN_RESET);
        if (htim3.Instance != nullptr)
        {
            __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 0U);
            htim3.Instance->CCER &= ~TIM_CCER_CC1E;
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

    void updateAdcHealth()
    {
        if (!adcStarted)
        {
            FaultManager::setBmsFault(FaultManager::BmsFault::AdcFault, true);
            return;
        }

        const uint32_t now = HAL_GetTick();
        if (now - lastAdcHealthCheckAt < ADC_HEALTH_CHECK_PERIOD_MS)
        {
            return;
        }
        lastAdcHealthCheckAt = now;

        if (isAdcHealthy())
        {
            adcHealthFailureCount = 0U;
            FaultManager::setBmsFault(FaultManager::BmsFault::AdcFault, false);
            return;
        }

        if (adcHealthFailureCount < ADC_HEALTH_FAILURE_LIMIT)
        {
            ++adcHealthFailureCount;
        }
        FaultManager::setBmsFault(FaultManager::BmsFault::AdcFault,
                                  adcHealthFailureCount >= ADC_HEALTH_FAILURE_LIMIT);
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

extern "C" void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    IO::reportAdcDmaCompletion(hadc);
}

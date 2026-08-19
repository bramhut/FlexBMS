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
        constexpr size_t ADC_CHANNEL_COUNT = 3U;
        constexpr size_t LOAD_VOLTAGE_INDEX = 0U;
        constexpr size_t BATTERY_VOLTAGE_INDEX = 1U;
        constexpr size_t VREFINT_INDEX = 2U;
        constexpr double ADC_OVERSAMPLING_GAIN = 16.0;
        constexpr int32_t ADC_DIFFERENTIAL_ZERO_CODE = 2048 * 16;
        constexpr double ADC_DIFFERENTIAL_SCALE = 2048.0 * ADC_OVERSAMPLING_GAIN;
        constexpr double HV_DIVIDER_AND_GAIN = 801.0 / 2.0;
        constexpr uint32_t ADC_HEALTH_CHECK_PERIOD_MS = 20U;
        // The scan itself runs at 50 Hz. This timeout leaves margin for the
        // ADC/DMA completion phase relative to the main task's 20 ms wake-up.
        constexpr uint32_t ADC_DMA_STALE_TIMEOUT_MS = 100U;
        constexpr uint8_t ADC_HEALTH_FAILURE_LIMIT = 3U;
        constexpr double MIN_VALID_ADC_REFERENCE = 2.8;
        constexpr double MAX_VALID_ADC_REFERENCE = 3.0;

        alignas(4) volatile uint32_t adcDmaValues[ADC_CHANNEL_COUNT] = {};
        volatile uint32_t adcValues[ADC_CHANNEL_COUNT] = {};
        volatile uint32_t adcSnapshotVersion = 0U;
        volatile uint32_t lastAdcDmaCompletionAt = 0U;
        bool adcStarted = false;
        uint32_t lastAdcHealthCheckAt = 0U;
        uint8_t adcHealthFailureCount = 0U;
        RGB_t ledColor{1.0, 1.0, 1.0};
        bool ledState = false;

        bool readAdcSnapshot(uint32_t (&values)[ADC_CHANNEL_COUNT])
        {
            for (uint8_t attempt = 0U; attempt < 3U; ++attempt)
            {
                const uint32_t begin = adcSnapshotVersion;
                if ((begin & 1U) != 0U)
                {
                    continue;
                }

                __DMB();
                for (size_t index = 0U; index < ADC_CHANNEL_COUNT; ++index)
                {
                    values[index] = adcValues[index];
                }
                __DMB();

                const uint32_t end = adcSnapshotVersion;
                if (begin == end && (end & 1U) == 0U)
                {
                    return true;
                }
            }

            return false;
        }

        // VREF+ is driven by the STM32's 2.9 V internal VREFBUF. Sampling
        // VREFINT therefore measures the ADC reference, not VDDA.
        double getAdcReferenceVoltage(const uint32_t (&values)[ADC_CHANNEL_COUNT])
        {
            const uint32_t vrefRaw = values[VREFINT_INDEX];
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
            uint32_t values[ADC_CHANNEL_COUNT] = {};
            if (!readAdcSnapshot(values))
            {
                return 0.0;
            }

            const double adcReferenceVoltage = getAdcReferenceVoltage(values);
            const int32_t differentialRaw =
                static_cast<int32_t>(values[index] & 0xFFFFU) -
                ADC_DIFFERENTIAL_ZERO_CODE;
            const double adcDifferentialVoltage =
                static_cast<double>(differentialRaw) * adcReferenceVoltage /
                ADC_DIFFERENTIAL_SCALE;
            return std::max(0.0, adcDifferentialVoltage * HV_DIVIDER_AND_GAIN);
        }

        double getHVVoltage(const uint32_t (&values)[ADC_CHANNEL_COUNT], size_t index)
        {
            const double adcReferenceVoltage = getAdcReferenceVoltage(values);
            const int32_t differentialRaw =
                static_cast<int32_t>(values[index] & 0xFFFFU) -
                ADC_DIFFERENTIAL_ZERO_CODE;
            const double adcDifferentialVoltage =
                static_cast<double>(differentialRaw) * adcReferenceVoltage /
                ADC_DIFFERENTIAL_SCALE;
            return std::max(0.0, adcDifferentialVoltage * HV_DIVIDER_AND_GAIN);
        }

        bool isAdcHealthy(uint32_t now)
        {
            const bool dmaIsFresh =
                now - lastAdcDmaCompletionAt <= ADC_DMA_STALE_TIMEOUT_MS;

            uint32_t values[ADC_CHANNEL_COUNT] = {};
            const bool snapshotAvailable = readAdcSnapshot(values);
            const double adcReferenceVoltage =
                snapshotAvailable ? getAdcReferenceVoltage(values) : 0.0;
            return adcStarted && dmaIsFresh && snapshotAvailable &&
                   HAL_ADC_GetError(&hadc1) == HAL_ADC_ERROR_NONE &&
                   hadc1.DMA_Handle != nullptr &&
                   HAL_DMA_GetError(hadc1.DMA_Handle) == HAL_DMA_ERROR_NONE &&
                   adcReferenceVoltage >= MIN_VALID_ADC_REFERENCE &&
                   adcReferenceVoltage <= MAX_VALID_ADC_REFERENCE;
        }
    }

    void reportAdcDmaCompletion(ADC_HandleTypeDef *hadc)
    {
        if (hadc->Instance == ADC1)
        {
            ++adcSnapshotVersion;
            __DMB();
            for (size_t index = 0U; index < ADC_CHANNEL_COUNT; ++index)
            {
                adcValues[index] = adcDmaValues[index];
            }
            __DMB();
            ++adcSnapshotVersion;
            lastAdcDmaCompletionAt = HAL_GetTick();
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
                         const_cast<uint32_t *>(adcDmaValues),
                         ADC_CHANNEL_COUNT) == HAL_OK;
        if (adcStarted)
        {
            adcStarted = HAL_TIM_Base_Start(&htim7) == HAL_OK;
        }
        if (!adcStarted)
        {
            PRINTF_ERR("[IO] ADC1 calibration, DMA, or 50 Hz trigger startup failed\n");
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

        if (isAdcHealthy(now))
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

    HVVoltages getHVVoltages()
    {
        uint32_t values[ADC_CHANNEL_COUNT] = {};
        const uint32_t now = HAL_GetTick();
        if (!adcStarted || now - lastAdcDmaCompletionAt > ADC_DMA_STALE_TIMEOUT_MS || !readAdcSnapshot(values))
        {
            return {};
        }

        const double adcReferenceVoltage = getAdcReferenceVoltage(values);
        if (adcReferenceVoltage < MIN_VALID_ADC_REFERENCE || adcReferenceVoltage > MAX_VALID_ADC_REFERENCE)
        {
            return {};
        }

        return {
            true,
            getHVVoltage(values, BATTERY_VOLTAGE_INDEX),
            getHVVoltage(values, LOAD_VOLTAGE_INDEX),
        };
    }
}

extern "C" void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    IO::reportAdcDmaCompletion(hadc);
}

// This file contains the peripheral IO definitions for the BMS Master board

#include "BoardIO.h"
#include "tim.h"

#define DEBUG_LVL 0
#include "Debug.h"

namespace IO
{
	/* GLOBALS */
	RGB_t _ledColor;
	bool _ledState;

	std::vector<std::function<void(bool state)>> _switchCallbacks;

	/* FUNCTIONS */

	void setup()
	{
		setRelay(false);
		setLED(false);
		setLEDcolor({255, 255, 255});
	}

	void onSwitchChange(std::function<void(bool state)> callback)
	{
		_switchCallbacks.push_back(callback);
	}

    bool getSwitch()
    {
        return READ_PIN(SW1_IN);
    }

    void _handleCallbacks(uint16_t GPIO_Pin)
	{
		if (GPIO_Pin == SW1_IN_Pin)
		{
			for (auto &callback : _switchCallbacks)
			{
				callback(getSwitch());
			}
		}
	}

	void setLEDcolor(RGB_t color)
	{
		_ledColor = color;
		setLED(_ledState);
	}

	void setLED(bool state)
	{
		_ledState = state;
		auto color = _ledColor;

		TIM1->CCR1 = color.r;
		TIM1->CCR2 = color.g;
		TIM1->CCR3 = color.b;
		_activatePWM(&htim1, TIM_CHANNEL_1, state);
		_activatePWM(&htim1, TIM_CHANNEL_2, state);
		_activatePWM(&htim1, TIM_CHANNEL_3, state);
	}

    void setRelay(bool state)
    {
		WRITE_PIN(RELAY, state);
    }
}

extern "C" void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    IO::_handleCallbacks(GPIO_Pin);
}

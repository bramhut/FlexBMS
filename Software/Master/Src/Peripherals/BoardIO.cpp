// This file contains the peripheral IO definitions for the BMS Master board

#include "BoardIO.h"
#include "tim.h"
#include "AIN.h"
#include <algorithm>
#include "TimeFunctions.h"

#define DEBUG_LVL 0
#include "Debug.h"

namespace IO
{
	/* GLOBALS */
	RGB_t _ledColor;
	bool _ledState;
	uint32_t lastPrintTime = 0;

	std::vector<std::function<void(bool state)>> _switchCallbacks;

	/* FUNCTIONS */

	void setup()
	{
		setRelay(false);
		setLED(false);
		setLEDcolor(RGB_t{1, 1, 1});
	}

	void onSwitchChange(std::function<void(bool state)> callback)
	{
		_switchCallbacks.push_back(callback);
	}

	bool getSwitch()
	{
		return !READ_PIN(SW1_IN);
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

	void setLEDcolor(HSV_t color)
	{
		_ledColor = hsv2rgb(color);
		setLED(_ledState);
	}

	void setLED(bool state)
	{
		_ledState = state;
		auto color = _ledColor;

		TIM1->CCR1 = std::clamp<double>(color.r * 255, 0, 255);
		TIM1->CCR2 = std::clamp<double>(color.g * 255, 0, 255);
		TIM1->CCR3 = std::clamp<double>(color.b * 255, 0, 255);
		_activatePWM(&htim1, TIM_CHANNEL_1, state);
		_activatePWM(&htim1, TIM_CHANNEL_2, state);
		_activatePWM(&htim1, TIM_CHANNEL_3, state);
	}

	void setRelay(bool state)
	{
		WRITE_PIN(RELAY, state);
	}

	void togglePrechargeAndAIR(bool state)
	{
		WRITE_PIN(PCC_ACT, state); // Set the PCC state to true
	}

	bool getFSDCState()
	{
		return READ_PIN(SDC_PCC);
	}

	double getCurrent()
	{
		double smallCurrentOffset = 1.298;	 // Offset in V
		double bigCurrentOffset = 0.852; // Offset in V
		double smallSensitivity = 0.049;				 // Sensitivity in V/A
		double bigSensitivity = 0.00220;			 // Sensitivity in V/A
		double maxSmallCurrent = -19.5; // Maximum current for small transducer in A

		AIN::measureVREF();
		double smallCurrentInput = (AIN::getAbsoluteVoltage(0));
		double bigCurrentInput = (AIN::getAbsoluteVoltage(4));

		double smallCurrent = (smallCurrentInput - smallCurrentOffset) / smallSensitivity;
		double bigCurrent = ((bigCurrentInput - bigCurrentOffset) / bigSensitivity) ;

		double current;

		if (smallCurrent > maxSmallCurrent && smallCurrent < -maxSmallCurrent)
		{
			current =  static_cast<double>(static_cast<int>(smallCurrent * 10.)) / 10.;;
		}
		else
		{
			current =  static_cast<double>(static_cast<int>(bigCurrent * 10.)) / 10.;;
		}

		// HAL sensor is reversed.
		current = -current;
		return current;
	}

}

extern "C" void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
	IO::_handleCallbacks(GPIO_Pin);
}

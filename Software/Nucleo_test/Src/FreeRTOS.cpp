#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"
#include "AIN.h"
#include "Time.h"
#include "DIO.h"

osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
	.name = "defaultTask",
	.stack_size = 128 * 4,
	.priority = (osPriority_t) osPriorityNormal,
};

void DefaultTask(void *argument)
{
	AIN::begin();
	setupMicros();
	uint32_t lastMicros = 0, lastMillis = 0, curMicros = 0;
	static bool ledState = false, ledState2 = false;
	while(1)
	{
		if ((curMicros = micros()) - lastMicros > 1000000)
		{
			lastMicros = curMicros;
			WRITE_PIN(LD2, ledState);
			// lastMillis = millis();
			ledState = !ledState;
		}

		// if (millis() - lastMillis > 1000)
		// {
		// 	lastMillis = millis();
		// 	// WRITE_PIN(LD2, ledState2);
		// 	ledState2 = !ledState2;
		// }
	}
}

// Don't change the function name, it is called from the generated code
void MX_FREERTOS_Init() {
	defaultTaskHandle = osThreadNew(DefaultTask, NULL, &defaultTask_attributes);
}
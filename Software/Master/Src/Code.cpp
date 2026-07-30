#include "main.h"
#include "cmsis_os.h"
#include "TimeFunctions.h"
#include "BoardIO.h"
#include "SPIwrapper.h"
#include "USBCOM.h"
#include "CAN.h"
#include "BMSCompanion.h"
#include "pcc.h" 
#include "Charger.h"
// #include "WSEN_TIDS.h"

#define DEBUG_LVL 2
#include "Debug.h"
#include "bcc/SlaveController.h"

#ifdef COMMANDS
#include "Commands.h"
#endif


/*
	Main task handler and attributes. Don't edit if you are unsure what EXACTLY you are doing.
*/
osThreadId_t mainTaskHandle;
const osThreadAttr_t mainTask_attributes = {
	.name = "mainTask",
	.stack_size = 2048,
	.priority = (osPriority_t)osPriorityNormal,
};

CAN can(&hfdcan1);

Commands* commands = Commands::getInstance();

void mainTask(void *argument)
{
	IO::setup();
	IO::setLED(true);
	USBCOM::setup();
	can.setup();
	commands->setup();
	SlaveController::setup(&can);
	Charger::setup(&can);
	BMSCompanion::setup();
	PCC::setup(&can);

	HSV_t hsv{0, 1, 1};
	while (1)
	{
		Charger::loop();
		PCC::loop();
		
		IO::setLEDcolor(hsv);
		hsv.h += 2.4;
		if (hsv.h >= 360)
		{
			hsv.h = 0;
		}

		commands->loop();
		delay(20);
		
	}
}

// Don't change the function name, it is called from the generated code
void MX_FREERTOS_Init()
{
	// Start up the main task. If necessary you can add other tasks here as well.
	mainTaskHandle = osThreadNew(mainTask, NULL, &mainTask_attributes);
}

// FreeRTOS hooks
void vApplicationStackOverflowHook(TaskHandle_t xTask, signed char *pcTaskName)
{
	HALT_IF_DEBUGGING();
	while (1)
		;
}

void vApplicationMallocFailedHook(void)
{
	HALT_IF_DEBUGGING();
	while (1)
		;
}

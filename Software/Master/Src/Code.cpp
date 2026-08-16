#include "main.h"
#include "cmsis_os.h"
#include "TimeFunctions.h"
#include "BoardIO.h"
#include "SPIwrapper.h"
#include "USBCOM.h"
#include "usb_device.h"
#include "CAN.h"
#include "BmsUart.h"
#include "StatusLed.h"
#include "pcc.h" 
// #include "WSEN_TIDS.h"

#define DEBUG_LVL 2
#include "Debug.h"
#include "bcc/SlaveController.h"

#include <algorithm>

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
	StatusLed::setup();
	USBCOM::setup();
	MX_USB_Device_Init();
	can.setup();
	commands->setup();
	SlaveController::setup(&can);
	PCC::setup();
	BmsUart::setup();

	while (1)
	{
		PCC::loop();

		StatusLed::update();

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

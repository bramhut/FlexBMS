#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"
#include "AIN.h"
#include "Time.h"
#include "DIO.h"
#include "SPIwrapper.h"

#define DEBUG_LVL 2
#include "Debug.h"
#include "bcc/bcc.h"

osThreadId_t mainTaskHandle;
const osThreadAttr_t mainTask_attributes = {
	.name = "mainTask",
	.stack_size = 128 * 4,
	.priority = (osPriority_t) osPriorityNormal,
};

osThreadId_t testTaskHandle;
const osThreadAttr_t testTask_attributes = {
	.name = "testTask",
	.stack_size = 128 * 4,
	.priority = (osPriority_t) osPriorityNormal,
};

SPI spiTX(&hspi2, SPI::MASTER_TX, 0, SPI2_CS_GPIO_Port, SPI2_CS_Pin);
SPI spiRX(&hspi3, SPI::SLAVE_RX, 6);


void mainTask(void *argument)
{
	uint8_t test_arr[] = {0x01, 0x01, 0x08, 0x01, 0x30, 0x3C};
	uint32_t lastMicros = 0, lastMillis = 0, curMicros = 0;
	bool pinState = false;

	AIN::begin();
	BCC bcc = BCC(&spiTX, &spiRX);

	
	// spiTX.setup();
	// spiRX.setup();

	// spiRX.addRXCallback([&test_arr](uint8_t *data, size_t size) {
	// 	PRINTF_INFO("[SPI] Received %d bytes\n", size);
	// 	for (size_t i = 0; i < size; i++)
	// 	{
	// 		PRINTF_INFO("%02X ", data[i]);
	// 	}
	// 	PRINTF_INFO("\n");
	// 	memcpy(test_arr, data, size);
	// 	test_arr[0] += 1;
	// });

	while(1)
	{

		// SPI TESTING
		// if (millis() - lastMillis >= 1)
		// {
		// 	lastMillis = millis();
		// 	spiTX.transmitGuaranteed(test_arr, sizeof(test_arr));
		// }
		// spiTX.loop();
		// spiRX.loop();


		// TIMER TESTING
		// if ((curMicros = micros()) - lastMicros >= 250)
		// {
		// 	lastMicros = curMicros;
		// 	WRITE_PIN(SPI2_CS, pinState);
		// 	pinState = !pinState;
		// }
		WRITE_PIN(SPI2_CS, 1);
		HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_13);
		// delay(100);
		// WRITE_PIN(SPI2_CS, 0);
	}
}

// Don't change the function name, it is called from the generated code
// Start up tasks
void MX_FREERTOS_Init() {
	mainTaskHandle = osThreadNew(mainTask, NULL, &mainTask_attributes);
	// testTaskHandle = osThreadNew(testTask, NULL, &testTask_attributes);
}
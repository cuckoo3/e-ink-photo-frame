#include <string.h>
#include "esp_log.h"
#include "pindefine.h"

#include "display_handler.hpp"

extern "C" {
	#include "GDEP133C02.h"
	#include "comm.h"
}

inline static constexpr char TAG[] ="DISPLAY_HANDLER";

void DisplayHandler::init_display()
{
	// EPD Hardware Initialization
	initialGpio();
	initialSpi();
	setGpioLevel(LOAD_SW, GPIO_HIGH);
	epdHardwareReset();
	setPinCsAll(GPIO_HIGH);
	initEPD();
	ESP_LOGI(TAG, "Initiate e-ink display done");
}

void DisplayHandler::clear_screen_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Async screen clear starting...");
    epdDisplayColor(WHITE); // Runs in its own thread, blocking only this task
    ESP_LOGI(TAG, "Async screen clear complete.");
    
    // Self-terminate task when finished
    vTaskDelete(NULL);
}

// Public non-blocking function
void DisplayHandler::clearScreenAsync(void)
{
    // Spawns the task and returns immediately!
    xTaskCreate(
        clear_screen_task,   // Task function
        "clear_screen_task", // Name for debugging
        4096,                // Stack size in words
        NULL,                // Parameter
        5,                   // Priority
        NULL                 // Task handle (not needed)
    );
}
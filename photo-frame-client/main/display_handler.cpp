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

void DisplayHandler::_clear_screen_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Async screen clear starting...");
    epdDisplayColor(WHITE); // Runs in its own thread, blocking only this task
    ESP_LOGI(TAG, "Async screen clear complete.");
    
	// Clear handle before task termination
	TaskHandle_t local_handle = s_clear_screen_task_handle;
	s_clear_screen_task_handle = nullptr;
	
	vTaskDelete(local_handle);
}

// Public non-blocking function
void DisplayHandler::clearScreenAsync(void)
{
	// Prevent spawning duplicate tasks if one is already running
    if (s_clear_screen_task_handle != nullptr) {
        ESP_LOGW(TAG, "Screen clear task already running, skipping...");
        return;
    }
    // Spawns the task and returns immediately!
    BaseType_t ret =xTaskCreate(
        _clear_screen_task,   // Task function
        "clear_screen_task", // Name for debugging
        4096,                // Stack size in words
        NULL,                // Parameter
        5,                   // Priority
		&s_clear_screen_task_handle      // Task handle (not needed)
    );
	if (ret != pdPASS) {
		ESP_LOGE(TAG, "Failed to create clear_screen_task");
		s_clear_screen_task_handle = nullptr;
    }
}

void DisplayHandler::display_image(const char *filepath)
{
	pic_display_from_file(filepath);
}

void DisplayHandler::display_image_array(const unsigned char *gImage)
{
	pic_display((const unsigned char *)gImage);
}

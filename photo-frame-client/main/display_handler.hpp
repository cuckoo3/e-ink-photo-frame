#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class DisplayHandler {
	public:
		/**
		 * @brief Initialize the e-ink display
		 *
		 * Performs hardware initialization including GPIO setup, SPI initialization,
		 * power management, and EPD initialization.
		 */
		static void init_display();
		static void clearScreenAsync();
		void display_image(const char *filepath);
		void display_image_array(const unsigned char *gImage);
	
	private:
		inline static TaskHandle_t s_clear_screen_task_handle = nullptr;
		
		/**
		 * @brief Task function to clear the screen asynchronously
		 *
		 * This function runs in its own FreeRTOS task and clears the e-ink display
		 * to white. It self-terminates after completion.
		 *
		 * @param pvParameters Unused parameter for task creation
		 */
		static void _clear_screen_task(void *pvParameters);
		
};


#pragma once
#include "esp_wifi.h"

class DppEnrollee {
	public:
		/**
		 * @brief Initialize the DPP Enrollee
		 *
		 * Sets up the DPP enrollee with the provided WiFi configuration.
		 * This includes registering event handlers and starting the DPP
		 * bootstrapping process.
		 *
		 * @param wifi_config Pointer to the WiFi configuration structure
		 */
		bool dpp_enrollee_init(wifi_config_t *wifi_config);
	private:
		inline static bool s_is_dpp_mode = false;
		/**
		 * @brief Event handler for DPP and WiFi events
		 *
		 * Handles various events related to DPP authentication, connection,
		 * and IP acquisition. Updates the internal state and event group
		 * accordingly.
		 *
		 * @param arg User-defined argument (unused)
		 * @param event_base The base of the event (e.g., WIFI_EVENT, IP_EVENT)
		 * @param event_id The specific event ID
		 * @param event_data Pointer to event-specific data
		 */
		static void event_handler(void *arg, esp_event_base_t event_base,
								  int32_t event_id, void *event_data);
		
		esp_err_t dpp_enrollee_bootstrap(void);
		
		void start_dpp_flow(void);
};

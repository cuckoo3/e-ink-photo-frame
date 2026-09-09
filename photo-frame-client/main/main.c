#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_log.h"

// --- Good-Display EPD related headers ---
#include "dpp_enrollee.h"
#include "server_comm.h"
#include "display_handler.h"

static const char *TAG = "MAIN";

wifi_config_t esp32_wifi_config;
esp_netif_ip_info_t ip_info;

static void on_got_ip_handler(void* arg, esp_event_base_t event_base, 
                             int32_t event_id, void* event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        
        ESP_LOGI(TAG, "Got IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
		memcpy(&ip_info, &event->ip_info, sizeof(esp_netif_ip_info_t));
        
		// Non-blocking trigger—returns instantly so network routines can run in parallel!
		clearScreenAsync();
		// server_comm_start(&event->ip_info.ip);
    }
}

void app_main(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
	
	init_display();
	
	// 1. Create event loop first
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	// 2. Register main.c handler for IP_EVENT_STA_GOT_IP
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, 
        IP_EVENT_STA_GOT_IP, 
        &on_got_ip_handler, 
        NULL, 
        NULL
    ));

    // 3. Now run DPP provisioning/connection (blocking call)
    dpp_enrollee_init(&esp32_wifi_config); 
}



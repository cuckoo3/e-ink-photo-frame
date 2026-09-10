#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_log.h"

// --- Good-Display EPD related headers ---
#include "display_handler.hpp"
#include "dpp_enrollee.hpp"
#include "server_comm.hpp"

extern "C" {
	#include "GDEP133C02.h"
	#include "comm.h"
}

inline static constexpr char TAG[] ="MAIN";

DisplayHandler displayHandler;
wifi_config_t esp32_wifi_config;
esp_netif_ip_info_t ip_info;
ServerComm serverComm;

char server_ip_str[16] = {0};
int server_http_port = 0;

static void on_got_ip_handler(void* arg, esp_event_base_t event_base, 
                             int32_t event_id, void* event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        
        ESP_LOGI(TAG, "Got IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
		memcpy(&ip_info, &event->ip_info, sizeof(esp_netif_ip_info_t));
        
		// Non-blocking trigger—returns instantly so network routines can run in parallel!
		displayHandler.clearScreenAsync();
		// server_comm_start(&event->ip_info.ip);
		
		//TODO: double check the logic of dppEnrolle and try to remove the event handle in main. move to the app_main after dpp_enrollee_init;
		serverComm.connect_server(server_ip_str, server_http_port);
		ESP_LOGI(TAG, "Discovered Server IP: %s, HTTP Port: %d", server_ip_str, server_http_port);
    }
}

extern "C" void app_main(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
	
	displayHandler.init_display();
	
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
    DppEnrollee dppEnrolle;
	dppEnrolle.dpp_enrollee_init(&esp32_wifi_config); 
	
	
}



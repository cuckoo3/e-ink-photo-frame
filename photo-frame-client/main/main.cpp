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

	DppEnrollee dppEnrolle;
    bool is_connected = dppEnrolle.dpp_enrollee_init(&esp32_wifi_config); 

    if (!is_connected) {
        ESP_LOGE(TAG, "Failed to establish Wi-Fi within 2 minutes. Entering deep sleep...");
        // Option: Put ESP32 into deep sleep or show an error screen on EPD
        return;
    }

    // Proceeds only when connected
    ESP_LOGI(TAG, "Wi-Fi ready. Starting server discovery...");
    displayHandler.clearScreenAsync();
    serverComm.connect_server(server_ip_str, server_http_port);

	
	
}



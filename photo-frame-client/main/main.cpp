#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_littlefs.h"

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

void init_littlefs(void)
{
	esp_vfs_littlefs_conf_t conf = {}; // Zero-initialize all members
	conf.base_path = "/data";             // Mount point path
	conf.partition_label = "storage";     // Matching partitions.csv name
	conf.format_if_mount_failed = true;   // Auto-format on first boot
	conf.dont_mount = false;

    esp_err_t ret = esp_vfs_littlefs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find LittleFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
        }
        return;
    }

    size_t total = 0, used = 0;
    esp_littlefs_info(conf.partition_label, &total, &used);
    ESP_LOGI(TAG, "Partition size: Total: %d KB, Used: %d KB", total / 1024, used / 1024);
}

extern "C" void app_main(void)
{
    // 1. Initialize NVS (Required for Wi-Fi and system settings)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
	
	// 2. Mount LittleFS storage partition
	init_littlefs();
	
	displayHandler.init_display();
	
	// 1. Create event loop first
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	DppEnrollee dppEnrolle;
    bool is_connected = dppEnrolle.dpp_enrollee_init(&esp32_wifi_config); 

    if (!is_connected) {
        ESP_LOGE(TAG, "Failed to establish Wi-Fi within 5 minutes. Entering deep sleep...");
        // Option: Put ESP32 into deep sleep or show an error screen on EPD
        return;
    }

    // Proceeds only when connected
    ESP_LOGI(TAG, "Wi-Fi ready. Starting server discovery...");
	if (dppEnrolle.is_dpp_mode())
    		displayHandler.clearScreenAsync();

    serverComm.connect_server(server_ip_str, server_http_port);

	char* filepath = serverComm.get_image(server_ip_str, server_http_port);
	if (filepath == NULL) {
		ESP_LOGE(TAG, "Failed to retrieve image from server.");
		return;
	}
	ESP_LOGI(TAG, "Image stored successfully. filepath: %s", filepath);
	displayHandler.display_image(filepath);
}



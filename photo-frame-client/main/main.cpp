#include <vector>
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_littlefs.h"
#include "esp_sleep.h"

#include "app_config.hpp"
#include "display_handler.hpp"
#include "dpp_enrollee.hpp"
#include "server_comm.hpp"
#include "file_handler.hpp"

extern "C" {
	#include "GDEP133C02.h"
	#include "comm.h"
}

inline static constexpr char TAG[] ="MAIN";

// Sleep duration: 4 hours in microseconds
//inline static constexpr uint64_t SLEEP_DURATION_US = 4ULL * 3600ULL * 1000000ULL;	// Sleep duration: 4 hours in microseconds
inline static constexpr uint64_t SLEEP_DURATION_US = 60 * 1000000ULL;					// sleep 1 minutes for testing

DisplayHandler displayHandler;
wifi_config_t esp32_wifi_config;
esp_netif_ip_info_t ip_info;
ServerComm serverComm;

// variables that stored in RTC memory which will not cleared in deep sleep 
RTC_DATA_ATTR char server_ip_str[16] = {0};
RTC_DATA_ATTR int server_http_port = 0;
RTC_DATA_ATTR static size_t current_playlist_index = 0;
RTC_DATA_ATTR static size_t total_playlist_count = 0;


void init_littlefs(void)
{
	esp_vfs_littlefs_conf_t conf = {}; // Zero-initialize all members
	conf.base_path = AppConfig::STORAGE_PATH;             // Mount point path
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
	DppEnrollee dppEnrolle;
	FileHandler fileHandler;
	
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
	
	// 3. Initialize default event loop cleanly
	esp_err_t event_err = esp_event_loop_create_default();
    if (event_err != ESP_OK && event_err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(event_err);
    }

    bool is_connected = dppEnrolle.dpp_enrollee_init(&esp32_wifi_config); 

    if (!is_connected) {
        ESP_LOGE(TAG, "Failed to establish Wi-Fi within 5 minutes. Entering deep sleep...");
        esp_deep_sleep(SLEEP_DURATION_US);
        return;
    }

    // Proceeds only when connected
	dppEnrolle.sync_sntp_time();
    ESP_LOGI(TAG, "Wi-Fi ready. Starting server discovery...");
	
	if (dppEnrolle.is_dpp_mode())
    		displayHandler.clearScreenAsync();

    if (serverComm.connect_server(server_ip_str, server_http_port)){
		char current_filename[AppConfig::MAX_FILENAME_LEN] = {0};
		
		if (serverComm.sync_image_list(server_ip_str, server_http_port)){
			// has file changed, rescan file index
			ESP_LOGI(TAG, "Have file changes. Rescan file index");
			total_playlist_count = fileHandler.rebuild_playlist_index(AppConfig::STORAGE_PATH, current_filename);
			current_playlist_index = 0;
		}
		else {
			ESP_LOGI(TAG, "No file changes. Fetching current file from playlist.idx...");

            // 1. Fetch file name at current_playlist_index directly from playlist.idx (O(1) read)
            if (!fileHandler.get_current_playlist_file(AppConfig::STORAGE_PATH, current_playlist_index, current_filename, &total_playlist_count)) {
                ESP_LOGE(TAG, "Failed to read index %zu from playlist.idx. Triggering emergency rebuild...", current_playlist_index);

                // Fallback: If playlist.idx is missing or corrupted, scan and rebuild on the fly
                total_playlist_count = fileHandler.rebuild_playlist_index(AppConfig::STORAGE_PATH, current_filename);
                current_playlist_index = 0;
            }
		}
		if (current_filename[0] != '\0') {
			// Construct full path for the display handler (e.g. "/data/img_001.bin")
            char full_path[128];
            snprintf(full_path, sizeof(full_path), "%s/%s", AppConfig::STORAGE_PATH, current_filename);

		    ESP_LOGI(TAG, "Displaying image (%zu/%zu): %s",  current_playlist_index + 1, total_playlist_count, full_path);
            displayHandler.display_image(full_path);

            // Advance index for the next 4-hour wake cycle (wraps back to 0 at end)
            if (total_playlist_count > 0) {
                current_playlist_index = (current_playlist_index + 1) % total_playlist_count;
            }
		}
		else {
            ESP_LOGE(TAG, "No valid filename acquired for display.");
        }
	}
	
	dppEnrolle.log_current_time();
	ESP_LOGI(TAG, "Enter deep sleep");
	esp_deep_sleep(SLEEP_DURATION_US);	// sleep 1 minutes for testing
}



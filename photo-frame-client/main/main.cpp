#include <vector>
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_littlefs.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"

#include "app_config.hpp"
#include "display_handler.hpp"
#include "dpp_enrollee.hpp"
#include "server_comm.hpp"
#include "file_handler.hpp"
#include "pindefine.h"

extern "C" {
	#include "GDEP133C02.h"
	#include "comm.h"
}


// Ext1 Wakeup Bitmask for Active-High Triggers
#define BUTTON_WAKEUP_BITMASK ( (1ULL << SW2_WAKEUP)   | \
                                (1ULL << SW3_PREV_IMG) | \
                                (1ULL << SW4_NEXT_IMG) )

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
RTC_DATA_ATTR static uint16_t sleep_duration_min = 240; // Default: 240 min (4 hours)
								
// Wakeup Trigger Actions
typedef enum {
    WAKEUP_ACTION_NONE = 0,
    WAKEUP_ACTION_SW2_DEFAULT,
    WAKEUP_ACTION_SHOW_PREV_IMAGE,
    WAKEUP_ACTION_SHOW_NEXT_IMAGE,
    WAKEUP_ACTION_TIMER_REFRESH
} wakeup_action_t;

/**
 * Evaluates system wakeup cause and identifies button triggers.
 */
wakeup_action_t check_wakeup_reason(void)
{
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT1) {
        // Retrieve GPIO status bitmask responsible for Ext1 wakeup
        uint64_t wakeup_pin_mask = esp_sleep_get_ext1_wakeup_status();

        if (wakeup_pin_mask & (1ULL << SW3_PREV_IMG)) {
            ESP_LOGI(TAG, "Woken up by SW3 (IO13) -> Display Previous Image");
            return WAKEUP_ACTION_SHOW_PREV_IMAGE;
        } 
        else if (wakeup_pin_mask & (1ULL << SW4_NEXT_IMG)) {
            ESP_LOGI(TAG, "Woken up by SW4 (IO14) -> Display Next Image");
            return WAKEUP_ACTION_SHOW_NEXT_IMAGE;
        } 
        else if (wakeup_pin_mask & (1ULL << SW2_WAKEUP)) {
            ESP_LOGI(TAG, "Woken up by SW2 (IO12) -> General System Wakeup");
            return WAKEUP_ACTION_SW2_DEFAULT;
        }
    } 
    else if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
        ESP_LOGI(TAG, "Woken up by Timer -> Scheduled Display Refresh");
        return WAKEUP_ACTION_TIMER_REFRESH;
    }

    ESP_LOGI(TAG, "Cold boot or power-on reset sequence detected");
    return WAKEUP_ACTION_NONE;
}

void wakeup_calculate_playlist_index(wakeup_action_t wakeup_action)
{
    if (total_playlist_count > 0) {
        if (wakeup_action == WAKEUP_ACTION_SHOW_PREV_IMAGE) {
            // SW3: Move back 1 position
            current_playlist_index = (current_playlist_index + total_playlist_count - 1) % total_playlist_count;
            ESP_LOGI(TAG, "SW3 wakeup: Showing previous image -> Index: %zu", current_playlist_index);
        } 
        else if (wakeup_action == WAKEUP_ACTION_SW2_DEFAULT) {
            // SW2: Re-display current image (no change needed)
            ESP_LOGI(TAG, "SW2 wakeup: Re-displaying current image -> Index: %zu", current_playlist_index);
        }
        else if (wakeup_action == WAKEUP_ACTION_SHOW_NEXT_IMAGE || wakeup_action == WAKEUP_ACTION_TIMER_REFRESH) {
            // SW4 or Timer: Advance forward 1 position
            current_playlist_index = (current_playlist_index + 1) % total_playlist_count;
            ESP_LOGI(TAG, "Timer/SW4 wakeup: Displaying next image -> Index: %zu", current_playlist_index);
        }
    }
}

void reset_button_wakeups(void)
{
    // 1. Enable RTC pulldowns for active-high GPIOs to prevent floating state false-wakeups
    rtc_gpio_pullup_dis(SW2_WAKEUP);
    rtc_gpio_pulldown_en(SW2_WAKEUP);

    rtc_gpio_pullup_dis(SW3_PREV_IMG);
    rtc_gpio_pulldown_en(SW3_PREV_IMG);

    rtc_gpio_pullup_dis(SW4_NEXT_IMG);
    rtc_gpio_pulldown_en(SW4_NEXT_IMG);

    // 2. Configure Ext1 wakeup source (ANY_HIGH triggers when any designated button goes HIGH)
    esp_sleep_enable_ext1_wakeup(BUTTON_WAKEUP_BITMASK, ESP_EXT1_WAKEUP_ANY_HIGH);
}

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
	
	// 3. Evaluate Deep Sleep Wakeup Source
    wakeup_action_t wakeup_action = check_wakeup_reason();

    // Handle button navigation arithmetic on RTC index
    wakeup_calculate_playlist_index(wakeup_action);
	
	// 4. Initialize default event loop cleanly
	displayHandler.init_display();
	
	esp_err_t event_err = esp_event_loop_create_default();
    if (event_err != ESP_OK && event_err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(event_err);
    }

    bool is_connected = dppEnrolle.dpp_enrollee_init(&esp32_wifi_config); 

    if (!is_connected) {
		uint64_t sleep_duration_us = (uint64_t)sleep_duration_min * 60ULL * 1000000ULL;
        ESP_LOGE(TAG, "Failed to establish Wi-Fi within 5 minutes. Entering deep sleep for %u minutes (%llu us)...", sleep_duration_min, sleep_duration_us);
        esp_deep_sleep(sleep_duration_us);
        return;
    }

    // Proceeds only when connected
	dppEnrolle.sync_sntp_time();
    ESP_LOGI(TAG, "Wi-Fi ready. Starting server discovery...");
	
	if (dppEnrolle.is_dpp_mode())
    		displayHandler.clearScreenAsync();

	char current_filename[AppConfig::MAX_FILENAME_LEN] = {0};
	bool image_list_updated = false;
	
	// --- 5. SERVER CONNECTION & IMAGE SYNC ---
	ESP_LOGI(TAG, "Attempting server connection...");
	bool sync_success = false;

	// Check if we have a cached server IP from previous RTC memory
	if (server_ip_str[0] != '\0' && server_http_port > 0) {
		ESP_LOGI(TAG, "Using cached server address: %s:%d", server_ip_str, server_http_port);
		if (serverComm.connect_server(server_ip_str, server_http_port)) {
			ESP_LOGI(TAG, "Server connected. Syncing image list...");
			if (serverComm.sync_image_list(server_ip_str, server_http_port, total_playlist_count, sleep_duration_min)) {
				sync_success = true;
				if (serverComm.has_file_changes()) {
					ESP_LOGI(TAG, "Server reported file updates. Rebuilding local playlist index...");
					total_playlist_count = fileHandler.rebuild_playlist_index(AppConfig::STORAGE_PATH, current_filename);
					current_playlist_index = 0;
					image_list_updated = true;
				} else {
					ESP_LOGI(TAG, "Server sync complete. No file changes needed.");
				}
			}
		}
	}

	// If cached connection failed or no cached IP exists -> Retry via UDP Discovery
	if (!sync_success) {
		ESP_LOGW(TAG, "Cached connection failed or missing. Starting UDP server discovery...");
		// Reset cached RTC server parameters
		server_ip_str[0] = '\0';
		server_http_port = 0;

		if (serverComm.discover_server(server_ip_str, server_http_port)) {
			ESP_LOGI(TAG, "Discovered server at %s:%d. Attempting sync...", server_ip_str, server_http_port);
			if (serverComm.sync_image_list(server_ip_str, server_http_port, total_playlist_count, sleep_duration_min)) {
				sync_success = true;
				if (serverComm.has_file_changes()) {
					ESP_LOGI(TAG, "Server reported file updates. Rebuilding local playlist index...");
					total_playlist_count = fileHandler.rebuild_playlist_index(AppConfig::STORAGE_PATH, current_filename);
					current_playlist_index = 0;
					image_list_updated = true;
				}
			}
		} else {
			ESP_LOGW(TAG, "UDP server discovery timed out. Operating in offline mode.");
		}
	}

	// --- 6. DISPLAY RENDERING ---
	// If image list was not updated by server (or sync failed), fetch current image from local index
	if (!image_list_updated) {
		ESP_LOGI(TAG, "Fetching image at index %zu/%zu from local playlist.idx...", current_playlist_index, total_playlist_count);

		// Fetch file name at current_playlist_index directly from playlist.idx
		if (!fileHandler.get_current_playlist_file(AppConfig::STORAGE_PATH, current_playlist_index, current_filename, &total_playlist_count)) {
			ESP_LOGE(TAG, "Failed to read index %zu from playlist.idx. Triggering emergency rebuild...", current_playlist_index);

			// Fallback: If playlist.idx is missing or corrupted, scan and rebuild on the fly
			total_playlist_count = fileHandler.rebuild_playlist_index(AppConfig::STORAGE_PATH, current_filename);
			current_playlist_index = 0;
		}
	}

	if (current_filename[0] != '\0') {
		// Construct full path for the display handler
		char full_path[128];
		snprintf(full_path, sizeof(full_path), "%s/%s", AppConfig::STORAGE_PATH, current_filename);

		ESP_LOGI(TAG, "Displaying image (%zu/%zu): %s", current_playlist_index + 1, total_playlist_count, full_path);
		displayHandler.display_image(full_path);
	} else {
		ESP_LOGE(TAG, "No valid filename acquired for display.");
	}
	
	ESP_LOGI(TAG, "Display refresh complete. Preparing power-down sequence...");
	displayHandler.sleep();
	reset_button_wakeups();
	
	dppEnrolle.log_current_time();
	
	// Calculate sleep duration in microseconds: min * 60 sec * 1,000,000 us
	uint64_t sleep_duration_us = (uint64_t)sleep_duration_min * 60ULL * 1000000ULL;
	ESP_LOGI(TAG, "Entering deep sleep for %u minutes (%llu us)...", sleep_duration_min, sleep_duration_us);
	esp_deep_sleep(sleep_duration_us);
}



#include <string.h>
#include <stdbool.h>

extern "C" {
	#include "freertos/FreeRTOS.h"
	#include "freertos/task.h"
	#include "freertos/event_groups.h"
	#include "esp_event.h"
	#include "esp_dpp.h"
	#include "esp_log.h"
	#include "esp_netif_sntp.h"
	#include "esp_sntp.h"
	#include "GDEP133C02.h"
	#include "comm.h"
	#include "esp_wifi.h"
}

#include "app_config.hpp"
#include "wifi_qrcode.hpp"
#include "dpp_enrollee.hpp"

inline static constexpr char TAG[] ="DPP_ENROLLEE";
inline static constexpr std::size_t CURVE_SEC256R1_PKEY_HEX_DIGITS = 64;

wifi_config_t *s_dpp_wifi_config = NULL;

static int s_retry_num = 0;

/* FreeRTOS event group to signal network state */
static EventGroupHandle_t s_dpp_event_group;

inline static constexpr uint32_t DPP_CONNECTED_BIT    = BIT0;
inline static constexpr uint32_t DPP_CONNECT_FAIL_BIT = BIT1;
inline static constexpr uint32_t DPP_AUTH_FAIL_BIT    = BIT2;
inline static constexpr std::size_t WIFI_MAX_RETRY_NUM = 3;

void DppEnrollee::_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START: {
				// Only start DPP listening if explicitly in DPP provisioning mode
                if (s_is_dpp_mode) {
                    esp_err_t err = esp_supp_dpp_start_listen();
                    if (err == ESP_OK) {
                        ESP_LOGI(TAG, "Started listening for DPP Authentication");
                    } else {
                        // Log warning instead of triggering ESP_ERROR_CHECK abort
                        ESP_LOGW(TAG, "Failed to start DPP listening: %s", esp_err_to_name(err));
                    }
                } else {
                    ESP_LOGI(TAG, "STA started in NVS mode. Connecting...");
                    esp_wifi_connect();
                }
                break;
            }
            case WIFI_EVENT_STA_DISCONNECTED: {
                if (s_retry_num < WIFI_MAX_RETRY_NUM) {
                    esp_wifi_connect();
                    s_retry_num++;
                    ESP_LOGI(TAG, "Disconnected, retrying (%d/%d)...", s_retry_num, WIFI_MAX_RETRY_NUM);
                } else {
                    ESP_LOGW(TAG, "Wi-Fi Connection failed.");
                    xEventGroupSetBits(s_dpp_event_group, DPP_CONNECT_FAIL_BIT);
                }
                break;
            }
            case WIFI_EVENT_STA_CONNECTED: {
                ESP_LOGI(TAG, "Connected to AP SSID: %s", s_dpp_wifi_config->sta.ssid);
                break;
            }
            case WIFI_EVENT_DPP_URI_READY: {
				wifi_event_dpp_uri_ready_t *uri_data = (wifi_event_dpp_uri_ready_t *)event_data;
                if (uri_data != NULL) {
                    ESP_LOGI(TAG, "Scan below QR Code to configure the enrollee:");
                    // Dynamically allocate or call without stack inflation
                    WifiQrcode *wifiQrcode = new WifiQrcode();
                    wifiQrcode->show_epd_qr_code((const char *)uri_data->uri);
                    delete wifiQrcode;
                }
                break;
            }
            case WIFI_EVENT_DPP_CFG_RECVD: {
                wifi_event_dpp_config_received_t *config = (wifi_event_dpp_config_received_t *)event_data;
	            
				// 1. Ensure s_dpp_wifi_config is allocated
                if (s_dpp_wifi_config == nullptr) {
                    s_dpp_wifi_config = new wifi_config_t();
                }
				
                memcpy(s_dpp_wifi_config, &config->wifi_cfg, sizeof(*s_dpp_wifi_config));
                s_retry_num = 0;
				
                esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, s_dpp_wifi_config);
                if (err == ESP_OK) {
                    ESP_LOGI(TAG, "Saved new DPP config to NVS. Connecting to SSID: %s", s_dpp_wifi_config->sta.ssid);
                    esp_wifi_connect();
                } else {
                    ESP_LOGE(TAG, "Failed to save Wi-Fi config: %s", esp_err_to_name(err));
                }
                break;
            }
            case WIFI_EVENT_DPP_FAILED: {
                wifi_event_dpp_failed_t *dpp_failure = (wifi_event_dpp_failed_t *)event_data;
                if (s_retry_num < 5) {
                    ESP_LOGI(TAG, "DPP Auth failed (%s), retrying...", esp_err_to_name((int)dpp_failure->failure_reason));
                    esp_supp_dpp_start_listen();
                    s_retry_num++;
                } else {
                    xEventGroupSetBits(s_dpp_event_group, DPP_AUTH_FAIL_BIT);
                }
                break;
            }
            default:
                break;
        }
    }
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_dpp_event_group, DPP_CONNECTED_BIT);
    }
}

esp_err_t DppEnrollee::_dpp_enrollee_bootstrap(void)
{
    esp_err_t ret;
    size_t pkey_len = strlen(CONFIG_ESP_DPP_BOOTSTRAPPING_KEY);

    if (pkey_len) {
        /* Currently only NIST P-256 curve is supported, add prefix/postfix accordingly */
        char prefix[] = "30310201010420";
        char postfix[] = "a00a06082a8648ce3d030107";

        if (pkey_len != CURVE_SEC256R1_PKEY_HEX_DIGITS) {
            ESP_LOGI(TAG, "Invalid key length! Private key needs to be 32 bytes (or 64 hex digits) long");
            return ESP_FAIL;
        }

		size_t key_len = strlen(prefix) + pkey_len + strlen(postfix) + 1;
		char *key = (char *)malloc(key_len);
		
        if (!key) {
            ESP_LOGE(TAG, "Failed to allocate memory for key");
            return ESP_ERR_NO_MEM;
        }
        snprintf(key, key_len, "%s%s%s", prefix, CONFIG_ESP_DPP_BOOTSTRAPPING_KEY, postfix);
        /* Currently only supported method is QR Code */
	    ret = esp_supp_dpp_bootstrap_gen(CONFIG_ESP_DPP_LISTEN_CHANNEL_LIST, DPP_BOOTSTRAP_QR_CODE, key, CONFIG_ESP_DPP_DEVICE_INFO);
        free(key);
    } else {
        ret = esp_supp_dpp_bootstrap_gen(CONFIG_ESP_DPP_LISTEN_CHANNEL_LIST, DPP_BOOTSTRAP_QR_CODE, NULL, CONFIG_ESP_DPP_DEVICE_INFO);
    }

    return ret;
}

void DppEnrollee::_start_dpp_flow(void)
{
    s_is_dpp_mode = true;
    s_retry_num = 0;
    
    ESP_LOGI(TAG, "Initializing DPP Enrollee and displaying QR Code...");
    ESP_ERROR_CHECK(esp_supp_dpp_init(NULL));
    ESP_ERROR_CHECK(_dpp_enrollee_bootstrap());
    
    // Stop Wi-Fi state machine and restart it in DPP listening mode
    esp_wifi_stop();
    ESP_ERROR_CHECK(esp_wifi_start());
}

bool DppEnrollee::dpp_enrollee_init(wifi_config_t *wifi_config)
{
    if (wifi_config != nullptr) {
        s_dpp_wifi_config = wifi_config;
    } else if (s_dpp_wifi_config == nullptr) {
        s_dpp_wifi_config = new wifi_config_t();
    }
	
    s_dpp_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

	// moved the event loop creation ESP_ERROR_CHECK(esp_event_loop_create_default()); to the main.c file to avoid the error "esp_event_loop_create_default() has already been called"
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }
	
    esp_netif_create_default_wifi_sta();

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &_event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	
	// Set 5-minute total budget (300 seconds)
	const TickType_t TOTAL_BUDGET_TICKS = pdMS_TO_TICKS(300 * 1000);
    TickType_t start_time = xTaskGetTickCount();
	
    // --- STEP 1: CHECK NVS FOR STORED CREDENTIALS ---
    wifi_config_t saved_config{};
    esp_err_t read_err = esp_wifi_get_config(WIFI_IF_STA, &saved_config);

    if (read_err == ESP_OK && strlen((const char *)saved_config.sta.ssid) > 0) {
        s_is_dpp_mode = false;
        ESP_LOGI(TAG, "Found stored Wi-Fi credentials for SSID: %s. Connecting...", saved_config.sta.ssid);
        memcpy(s_dpp_wifi_config, &saved_config, sizeof(wifi_config_t));
        ESP_ERROR_CHECK(esp_wifi_start());
    } else {
        ESP_LOGW(TAG, "No NVS credentials found. Starting DPP mode...");
        _start_dpp_flow();
    }

    bool connection_successful = false;

    while (true) {
        // Calculate remaining time in our 5-minute budget
        TickType_t elapsed = xTaskGetTickCount() - start_time;
        if (elapsed >= TOTAL_BUDGET_TICKS) {
            ESP_LOGE(TAG, "5-minute total time limit reached. Connection failed.");
            break;
        }

        TickType_t remaining_ticks = TOTAL_BUDGET_TICKS - elapsed;

        // Block for remaining time
        EventBits_t bits = xEventGroupWaitBits(s_dpp_event_group,
                                               DPP_CONNECTED_BIT | DPP_CONNECT_FAIL_BIT | DPP_AUTH_FAIL_BIT,
                                               pdTRUE,  // Clear bits
                                               pdFALSE, 
                                               remaining_ticks);

        if (bits & DPP_CONNECTED_BIT) {
            ESP_LOGI(TAG, "Wi-Fi Connected successfully!");
            connection_successful = true;
            break;
        } 
        else if (bits & DPP_CONNECT_FAIL_BIT) {
            // NVS connection failed! Fallback to DPP if time remains
            if (!s_is_dpp_mode) {
                ESP_LOGW(TAG, "NVS connection failed! Switching to DPP mode and rendering QR code...");
                _start_dpp_flow(); 
            } else {
                ESP_LOGE(TAG, "DPP connection attempt failed. Re-listening...");
                s_retry_num = 0;
                esp_supp_dpp_start_listen();
            }
        } 
        else if (bits & DPP_AUTH_FAIL_BIT) {
            ESP_LOGE(TAG, "DPP Authentication failed. Restarting listener...");
            s_retry_num = 0;
            esp_supp_dpp_start_listen();
        } 
        else {
            // bits == 0: Timeout reached during xEventGroupWaitBits
            ESP_LOGE(TAG, "5-minute total connection budget expired.");
            break;
        }
    }

    // --- CLEANUP (Do NOT unregister event handlers so auto-reconnect keeps working) ---
    if (s_is_dpp_mode) {
        esp_supp_dpp_deinit();
    }
    vEventGroupDelete(s_dpp_event_group);

    return connection_successful;
}

bool DppEnrollee::is_dpp_mode(void)
{
	return s_is_dpp_mode;
}

void DppEnrollee::sync_sntp_time()
{
    ESP_LOGI(TAG, "Initializing SNTP time sync...");

    // Default configuration uses 1 server ("pool.ntp.org")
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("time.google.com");

    esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SNTP: %s", esp_err_to_name(err));
        return;
    }

    int retry = 0;
    const int max_retries = 15; // 15-second timeout

    while (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(1000)) != ESP_OK && ++retry < max_retries) {
        ESP_LOGI(TAG, "Waiting for SNTP time sync... (%d/%d)", retry, max_retries);
    }

    if (retry >= max_retries) {
        ESP_LOGW(TAG, "SNTP time sync timed out. Proceeding without time update.");
    } else {
        ESP_LOGI(TAG, "SNTP time synced successfully.");
    }
}

void DppEnrollee::log_current_time(void)
{
	time_t now;
	struct tm timeinfo;
	time(&now);
	localtime_r(&now, &timeinfo);

	char time_str[32];
	asctime_r(&timeinfo, time_str);
	// Strip trailing newline character added by asctime_r for clean ESP_LOG output
	time_str[strcspn(time_str, "\r\n")] = '\0';

	ESP_LOGI(TAG, "Current time: %s", time_str);
}
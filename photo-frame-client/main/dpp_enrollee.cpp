/* DPP Enrollee Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <stdbool.h>

extern "C" {
	#include "freertos/FreeRTOS.h"
	#include "freertos/task.h"
	#include "freertos/event_groups.h"
	#include "esp_event.h"
	#include "esp_dpp.h"
	#include "esp_log.h"
	#include "GDEP133C02.h"
	#include "comm.h"
	#include "status.h"
	#include "esp_wifi.h"
}

#include "wifi_qrcode.hpp"
#include "dpp_enrollee.hpp"

inline static constexpr char TAG[] ="DPP_ENROLLEE";
inline static constexpr std::size_t CURVE_SEC256R1_PKEY_HEX_DIGITS = 64;

wifi_config_t *s_dpp_wifi_config=NULL;

static int s_retry_num = 0;

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_dpp_event_group;

inline static constexpr uint32_t DPP_CONNECTED_BIT=BIT0;
inline static constexpr uint32_t DPP_CONNECT_FAIL_BIT=BIT1;
inline static constexpr uint32_t DPP_AUTH_FAIL_BIT=BIT2;
inline static constexpr std::size_t WIFI_MAX_RETRY_NUM=3;

void DppEnrollee::event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
	WifiQrcode wifiQrcode;
	
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
                    ESP_LOGI(TAG, "STA started in NVS direct-connect mode. Skipping DPP listen.");
                }
                break;
			}
	        case WIFI_EVENT_STA_DISCONNECTED: {
	            if (s_retry_num < WIFI_MAX_RETRY_NUM) {
	                esp_wifi_connect();
	                s_retry_num++;
	                ESP_LOGI(TAG, "Disconnect event, retry to connect to the AP");
	            } else {
	                xEventGroupSetBits(s_dpp_event_group, DPP_CONNECT_FAIL_BIT);
	            }
	            break;
			}
	        case WIFI_EVENT_STA_CONNECTED:{
		    		ESP_LOGI(TAG, "Successfully connected to the AP ssid : %s ", s_dpp_wifi_config->sta.ssid);
				break;
			}
	        case WIFI_EVENT_DPP_URI_READY: {
				wifi_event_dpp_uri_ready_t *uri_data = (wifi_event_dpp_uri_ready_t *)event_data;
	            if (uri_data != NULL) {
	                ESP_LOGI(TAG, "Scan below QR Code to configure the enrollee:");
	                
	                // Trigger non-blocking EPD generation task
	                wifiQrcode.show_epd_qr_code((const char *)uri_data->uri);
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
				
	            esp_err_t err =esp_wifi_set_config(WIFI_IF_STA, s_dpp_wifi_config);
				if (err == ESP_OK) {
			        ESP_LOGI(TAG, "Successfully saved DPP Wi-Fi config to NVS for SSID: %s", s_dpp_wifi_config->sta.ssid);
			        esp_wifi_connect();
			    } else {
			        ESP_LOGE(TAG, "Failed to save Wi-Fi config: %s", esp_err_to_name(err));
			    }
	            break;
			}
	        case WIFI_EVENT_DPP_FAILED:{
	            wifi_event_dpp_failed_t *dpp_failure = (wifi_event_dpp_failed_t *)event_data;
	            if (s_retry_num < 5) {
	                ESP_LOGI(TAG, "DPP Auth failed (Reason: %s), retry...", esp_err_to_name((int)dpp_failure->failure_reason));
	                ESP_ERROR_CHECK(esp_supp_dpp_start_listen());
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
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_dpp_event_group, DPP_CONNECTED_BIT);
    }
}

esp_err_t DppEnrollee::dpp_enrollee_bootstrap(void)
{
    esp_err_t ret;
    size_t pkey_len = strlen(CONFIG_ESP_DPP_BOOTSTRAPPING_KEY);
    char *key = NULL;

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
            ESP_LOGI(TAG, "Failed to allocate for bootstrapping key");
            return ESP_ERR_NO_MEM;
        }
        sprintf(key, "%s%s%s", prefix, CONFIG_ESP_DPP_BOOTSTRAPPING_KEY, postfix);
    }

    /* Currently only supported method is QR Code */
    ret = esp_supp_dpp_bootstrap_gen(CONFIG_ESP_DPP_LISTEN_CHANNEL_LIST, DPP_BOOTSTRAP_QR_CODE,
                                     key, CONFIG_ESP_DPP_DEVICE_INFO);

    if (key)
        free(key);

    return ret;
}

void DppEnrollee::dpp_enrollee_init(wifi_config_t *wifi_config)
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

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	
	
	// --- CHECK NVS FOR EXISTING CREDENTIALS ---
    wifi_config_t saved_config{};
    esp_err_t read_err = esp_wifi_get_config(WIFI_IF_STA, &saved_config);

    // Check if SSID stored in NVS is valid (non-empty)
    if (read_err == ESP_OK && strlen((const char *)saved_config.sta.ssid) > 0) {
		s_is_dpp_mode = false;
        ESP_LOGI(TAG, "Found stored Wi-Fi credentials for SSID: %s. Connecting...", saved_config.sta.ssid);
        memcpy(s_dpp_wifi_config, &saved_config, sizeof(wifi_config_t));

        // Start Wi-Fi & connect directly without listening for DPP
        ESP_ERROR_CHECK(esp_wifi_start());
        esp_wifi_connect();
    } 
    else {
        // No saved credentials -> Start DPP Onboarding Listener
		s_is_dpp_mode = true;
        ESP_LOGI(TAG, "No stored Wi-Fi credentials found. Starting DPP Enrollee listener...");
	    ESP_ERROR_CHECK(esp_supp_dpp_init(NULL));
	    ESP_ERROR_CHECK(dpp_enrollee_bootstrap());
	    ESP_ERROR_CHECK(esp_wifi_start());
	}
	
	//TODO: if not able to connect wifi, show the qr code for dpp onboarding, and wait for the user to scan it and connect to the wifi

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_dpp_event_group,
                                           DPP_CONNECTED_BIT | DPP_CONNECT_FAIL_BIT | DPP_AUTH_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually happened. */
    if (bits & DPP_CONNECTED_BIT) {
		ESP_LOGI(TAG, "Wi-Fi Connected successfully!");
    } else if (bits & DPP_CONNECT_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", s_dpp_wifi_config->sta.ssid, s_dpp_wifi_config->sta.password);
    } else if (bits & DPP_AUTH_FAIL_BIT) {
        ESP_LOGI(TAG, "DPP Authentication failed after %d retries", s_retry_num);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    esp_supp_dpp_deinit();
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler));
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler));
    vEventGroupDelete(s_dpp_event_group);
}




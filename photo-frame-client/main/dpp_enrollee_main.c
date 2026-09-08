/* DPP Enrollee Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_dpp.h"
#include "esp_log.h"
#include "nvs_flash.h"


// --- Good-Display EPD related headers ---
#include "wifi_qrcode.h"
#include "display_handler.h"

#ifdef CONFIG_ESP_DPP_LISTEN_CHANNEL_LIST
#define EXAMPLE_DPP_LISTEN_CHANNEL_LIST     CONFIG_ESP_DPP_LISTEN_CHANNEL_LIST
#else
#define EXAMPLE_DPP_LISTEN_CHANNEL_LIST     "6"
#endif

#ifdef CONFIG_ESP_DPP_BOOTSTRAPPING_KEY
#define EXAMPLE_DPP_BOOTSTRAPPING_KEY   CONFIG_ESP_DPP_BOOTSTRAPPING_KEY
#else
#define EXAMPLE_DPP_BOOTSTRAPPING_KEY   0
#endif

#ifdef CONFIG_ESP_DPP_DEVICE_INFO
#define EXAMPLE_DPP_DEVICE_INFO      CONFIG_ESP_DPP_DEVICE_INFO
#else
#define EXAMPLE_DPP_DEVICE_INFO      0
#endif

#define CURVE_SEC256R1_PKEY_HEX_DIGITS     64

static const char *TAG = "wifi dpp-enrollee";
wifi_config_t s_dpp_wifi_config;

static int s_retry_num = 0;

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_dpp_event_group;

#define DPP_CONNECTED_BIT  BIT0
#define DPP_CONNECT_FAIL_BIT     BIT1
#define DPP_AUTH_FAIL_BIT           BIT2
#define WIFI_MAX_RETRY_NUM 3

#define EPD_WIDTH 1200         // Total display width (pixels)
#define EPD_HALF_HEIGHT 800        // Display height (pixels)



///* Custom callback function invoked by esp_qrcode_generate to render onto EPD */
//static void epd_qrcode_display_cb(esp_qrcode_handle_t qrcode)
//{
//    uint16_t qrcode_size = esp_qrcode_get_size(qrcode);
//
//    /*
//     * Keep the original QR scale for now.
//     *
//     * E6 format:
//     *   4 bits per pixel
//     *   2 pixels per byte
//     */
//    uint32_t qr_pixel_size = qrcode_size * QR_SCALE;
//
//    uint32_t width_bytes = (qr_pixel_size + 1) / 2;
//
//    uint32_t final_size = width_bytes * qr_pixel_size;
//
//    ESP_LOGI(TAG, "QR size=%u x %u, buffer=%lu bytes", qr_pixel_size, qr_pixel_size, (unsigned long)final_size);
//
//    uint8_t *final_buffer = (uint8_t *)malloc(final_size);
//
//    if (final_buffer == NULL) {
//        ESP_LOGE(TAG, "Failed to allocate QR buffer: %lu bytes", (unsigned long)final_size);
//        return;
//    }
//
//    /*
//     * Fill with WHITE.
//     *
//     * E6:
//     *   one byte = two 4-bit pixels
//     */
//    uint8_t white = WHITE & 0x0F;
//
//    memset(final_buffer, (white << 4) | white, final_size);
//
//    /*
//     * Render QR.
//     */
//	 for (uint16_t y = 0; y < qrcode_size; y++) {
//	     for (uint16_t x = 0; x < qrcode_size; x++) {
//
//	         if (!esp_qrcode_get_module(qrcode, x, y))
//	             continue;
//
//	         uint8_t black = BLACK & 0x0F;
//
//	         for (uint32_t sy = 0; sy < QR_SCALE; sy++) {
//	             for (uint32_t sx = 0; sx < QR_SCALE; sx++) {
//
//	                 uint32_t px = x * QR_SCALE + sx;
//	                 uint32_t py = y * QR_SCALE + sy;
//
//					 // 180° rotation
//					 uint32_t rotated_x = qr_pixel_size - 1 - px;
//					 uint32_t rotated_y = qr_pixel_size - 1 - py;
//
//					 // E6 data is stored right-to-left
//					 uint32_t reversed_x = qr_pixel_size - 1 - rotated_x;
//
//					 uint32_t byte_index =
//					     rotated_y * width_bytes +
//					     (reversed_x / 2);
//
//					 if ((reversed_x & 1) == 0) {
//					     // High nibble
//					     final_buffer[byte_index] &= 0x0F;
//					     final_buffer[byte_index] |= black << 4;
//					 } else {
//					     // Low nibble
//					     final_buffer[byte_index] &= 0xF0;
//					     final_buffer[byte_index] |= black;
//					 }
//	             }
//	         }
//	     }
//	 }
//
//    /*
//     * E6 logical area for one CS:
//     *
//     *   600 × 1600
//     */
//    unsigned int driver_xPixel = qr_pixel_size;
//    unsigned int driver_yLine  = qr_pixel_size;
//
//    if (driver_xPixel > 600 ||
//        driver_yLine > 1600) {
//
//        ESP_LOGE(TAG,
//                 "QR too large: %u x %u",
//                 driver_xPixel,
//                 driver_yLine);
//
//        free(final_buffer);
//        return;
//    }
//
//    /*
//     * Center horizontally.
//     *
//     * xStart must be divisible by 4.
//     */
//    unsigned int driver_xStart =
//        ((600 - driver_xPixel) / 2) & ~0x03;
//
//    /*
//     * Center vertically in 1600.
//     *
//     * yStart must be even.
//     */
//    unsigned int driver_yStart =
//        ((1600 - driver_yLine) / 2) & ~0x01;
//
//    ESP_LOGI(TAG,
//             "QR window: x=%u y=%u w=%u h=%u",
//             driver_xStart,
//             driver_yStart,
//             driver_xPixel,
//             driver_yLine);
//
//    /*
//     * White screen.
//     */
//    epdDisplayColor(WHITE);
//
//    /*
//     * Send image.
//     */
//    partialWindowUpdateWithImageData(
//        0,
//        final_buffer,
//        final_size,
//        driver_xStart,
//        driver_yStart,
//        driver_xPixel,
//        driver_yLine,
//        1
//    );
//
//    free(final_buffer);
//}
//
///* Blocking synchronous function to render QR code on EPD */
//static void show_epd_qr_code(const char *uri_string)
//{
//    if (uri_string == NULL) {
//        ESP_LOGE(TAG, "URI string is NULL!");
//        return;
//    }
//
//    ESP_LOGI(TAG, "Generating QR Code directly for E-Paper display...");
//
//    // 1. EPD Hardware Initialization
//    initialGpio();
//    initialSpi();
//    setGpioLevel(LOAD_SW, GPIO_HIGH);
//    epdHardwareReset();
//    setPinCsAll(GPIO_HIGH);
//    initEPD();
//
//    // 2. Configure Espressif QR Code generator
//    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
//    cfg.display_func = epd_qrcode_display_cb;
//    cfg.max_qrcode_version = 10;
//    cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
//
//    // 3. Generate QR code synchronously
//    esp_err_t ret = esp_qrcode_generate(&cfg, uri_string);
//    if (ret != ESP_OK) {
//        ESP_LOGE(TAG, "Failed to generate QR code: %s", esp_err_to_name(ret));
//    }
//}


static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_ERROR_CHECK(esp_supp_dpp_start_listen());
            ESP_LOGI(TAG, "Started listening for DPP Authentication");
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            if (s_retry_num < WIFI_MAX_RETRY_NUM) {
                esp_wifi_connect();
                s_retry_num++;
                ESP_LOGI(TAG, "Disconnect event, retry to connect to the AP");
            } else {
                xEventGroupSetBits(s_dpp_event_group, DPP_CONNECT_FAIL_BIT);
            }
            break;
        case WIFI_EVENT_STA_CONNECTED:
	    		ESP_LOGI(TAG, "Successfully connected to the AP ssid : %s ", s_dpp_wifi_config.sta.ssid);
			// clear the EPD display after successful connection
			clearScreen();
            
			break;
        case WIFI_EVENT_DPP_URI_READY:
			wifi_event_dpp_uri_ready_t *uri_data = event_data;
            if (uri_data != NULL) {
                ESP_LOGI(TAG, "Scan below QR Code to configure the enrollee:");
                
                // Trigger non-blocking EPD generation task
                show_epd_qr_code((const char *)uri_data->uri);
            }
            break;
//            wifi_event_dpp_uri_ready_t *uri_data = event_data;
//            if (uri_data != NULL) {
//                esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
//
//                ESP_LOGI(TAG, "Scan below QR Code to configure the enrollee:");
//                esp_qrcode_generate(&cfg, (const char *)uri_data->uri);
//            }
//            break;
        case WIFI_EVENT_DPP_CFG_RECVD:
            wifi_event_dpp_config_received_t *config = event_data;
            memcpy(&s_dpp_wifi_config, &config->wifi_cfg, sizeof(s_dpp_wifi_config));
            s_retry_num = 0;
            esp_wifi_set_config(ESP_IF_WIFI_STA, &s_dpp_wifi_config);
            esp_wifi_connect();
            break;
        case WIFI_EVENT_DPP_FAILED:
            wifi_event_dpp_failed_t *dpp_failure = event_data;
            if (s_retry_num < 5) {
                ESP_LOGI(TAG, "DPP Auth failed (Reason: %s), retry...", esp_err_to_name((int)dpp_failure->failure_reason));
                ESP_ERROR_CHECK(esp_supp_dpp_start_listen());
                s_retry_num++;
            } else {
                xEventGroupSetBits(s_dpp_event_group, DPP_AUTH_FAIL_BIT);
            }

            break;
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

esp_err_t dpp_enrollee_bootstrap(void)
{
    esp_err_t ret;
    size_t pkey_len = strlen(EXAMPLE_DPP_BOOTSTRAPPING_KEY);
    char *key = NULL;

    if (pkey_len) {
        /* Currently only NIST P-256 curve is supported, add prefix/postfix accordingly */
        char prefix[] = "30310201010420";
        char postfix[] = "a00a06082a8648ce3d030107";

        if (pkey_len != CURVE_SEC256R1_PKEY_HEX_DIGITS) {
            ESP_LOGI(TAG, "Invalid key length! Private key needs to be 32 bytes (or 64 hex digits) long");
            return ESP_FAIL;
        }

        key = malloc(sizeof(prefix) + pkey_len + sizeof(postfix));
        if (!key) {
            ESP_LOGI(TAG, "Failed to allocate for bootstrapping key");
            return ESP_ERR_NO_MEM;
        }
        sprintf(key, "%s%s%s", prefix, EXAMPLE_DPP_BOOTSTRAPPING_KEY, postfix);
    }

    /* Currently only supported method is QR Code */
    ret = esp_supp_dpp_bootstrap_gen(EXAMPLE_DPP_LISTEN_CHANNEL_LIST, DPP_BOOTSTRAP_QR_CODE,
                                     key, EXAMPLE_DPP_DEVICE_INFO);

    if (key)
        free(key);

    return ret;
}

void dpp_enrollee_init(void)
{
    s_dpp_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_supp_dpp_init(NULL));
    ESP_ERROR_CHECK(dpp_enrollee_bootstrap());
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_dpp_event_group,
                                           DPP_CONNECTED_BIT | DPP_CONNECT_FAIL_BIT | DPP_AUTH_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & DPP_CONNECTED_BIT) {
    } else if (bits & DPP_CONNECT_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 s_dpp_wifi_config.sta.ssid, s_dpp_wifi_config.sta.password);
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
    dpp_enrollee_init();
}

#include <stdint.h>
#include <stdbool.h>
#include "qrcode.h"

#include "esp_log.h"

#include "wifi_qrcode.hpp"

extern "C" {
	#include "GDEP133C02.h"
}

inline static constexpr char TAG[] ="WIFI_QRCODE";
/* Display dimensions */
inline static constexpr std::size_t SCREEN_HALF_WIDTH = 600;
inline static constexpr std::size_t SCREEN_WIDTH = 1200;
inline static constexpr std::size_t SCREEN_HEIGHT = 1600;

inline static constexpr std::size_t QR_SCALE = 8;

/* Custom callback function invoked by esp_qrcode_generate to render onto EPD */
void WifiQrcode::_epd_qrcode_display_cb(esp_qrcode_handle_t qrcode)
{
    uint16_t qrcode_size = esp_qrcode_get_size(qrcode);

    /*
     * Keep the original QR scale for now.
     *
     * E6 format:
     *   4 bits per pixel
     *   2 pixels per byte
     */
    uint32_t qr_pixel_size = qrcode_size * QR_SCALE;
    uint32_t width_bytes = (qr_pixel_size + 1) / 2;
    uint32_t final_size = width_bytes * qr_pixel_size;
	
	if (qr_pixel_size > SCREEN_HALF_WIDTH || qr_pixel_size > SCREEN_HEIGHT) {
		ESP_LOGE(TAG, "QR code dimension (%lu x %lu) exceeds display limits", 
		                 (unsigned long)qr_pixel_size, (unsigned long)qr_pixel_size);
	    return;
	}

    ESP_LOGI(TAG, "QR size=%u x %u, buffer=%lu bytes", qr_pixel_size, qr_pixel_size, (unsigned long)final_size);

	uint8_t *final_buffer = static_cast<uint8_t *>(malloc(final_size));

    if (final_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate QR buffer: %lu bytes", (unsigned long)final_size);
        return;
    }

    /*
     * Fill with WHITE.
     *
     * E6:
     *   one byte = two 4-bit pixels
     */
    uint8_t white = WHITE & 0x0F;

    memset(final_buffer, (white << 4) | white, final_size);

    /*
     * Render QR.
     */
	 for (uint16_t y = 0; y < qrcode_size; y++) {
	     for (uint16_t x = 0; x < qrcode_size; x++) {

	         if (!esp_qrcode_get_module(qrcode, x, y))
	             continue;

	         uint8_t black = BLACK & 0x0F;

	         for (uint32_t sy = 0; sy < QR_SCALE; sy++) {
	             for (uint32_t sx = 0; sx < QR_SCALE; sx++) {

	                 uint32_t px = x * QR_SCALE + sx;
	                 uint32_t py = y * QR_SCALE + sy;

					 // 180° rotation
					 uint32_t rotated_x = qr_pixel_size - 1 - px;
					 uint32_t rotated_y = qr_pixel_size - 1 - py;

					 uint32_t byte_index = rotated_y * width_bytes + (px / 2);

					 if ((px & 1) == 0) {
					     // High nibble
					     final_buffer[byte_index] &= 0x0F;
					     final_buffer[byte_index] |= black << 4;
					 } else {
					     // Low nibble
					     final_buffer[byte_index] &= 0xF0;
					     final_buffer[byte_index] |= black;
					 }
	             }
	         }
	     }
	 }

    /*
     * Center horizontally.
     *
     * xStart must be divisible by 4.
     */
    unsigned int driver_xStart = ((SCREEN_HALF_WIDTH - qr_pixel_size) / 2) & ~0x03;

    /*
     * Center vertically in SCREEN_HEIGHT.
     *
     * yStart must be even.
     */
    unsigned int driver_yStart = ((SCREEN_HEIGHT - qr_pixel_size) / 2) & ~0x01;

    ESP_LOGI(TAG, "QR window: x=%u y=%u w=%lu h=%lu",
             driver_xStart, driver_yStart, (unsigned long)qr_pixel_size, (unsigned long)qr_pixel_size);

    /*
     * White screen.
     */
    epdDisplayColor(WHITE);

    /*
     * Send image.
     */
    partialWindowUpdateWithImageData(
        0,
        final_buffer,
        final_size,
        driver_xStart,
        driver_yStart,
        qr_pixel_size,
        qr_pixel_size,
        1
    );

    free(final_buffer);
}

/* Blocking synchronous function to render QR code on EPD */
void WifiQrcode::show_epd_qr_code(const char *uri_string)
{
    if (uri_string == nullptr) {
        ESP_LOGE(TAG, "URI string is NULL!");
        return;
    }

    ESP_LOGI(TAG, "Generating QR Code directly for E-Paper display...");

    // 2. Configure Espressif QR Code generator
    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    cfg.display_func = _epd_qrcode_display_cb;
    cfg.max_qrcode_version = 10;
    cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;

    // 3. Generate QR code synchronously
    esp_err_t ret = esp_qrcode_generate(&cfg, uri_string);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to generate QR code: %s", esp_err_to_name(ret));
    }
}
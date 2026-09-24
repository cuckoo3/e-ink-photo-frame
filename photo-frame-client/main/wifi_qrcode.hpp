#pragma once
#include <stdbool.h>
#include "qrcode.h"

class WifiQrcode {
	public:
		/**
		 * @brief Display a WiFi QR code on the E-Paper display
		 *
		 * Generates and renders a QR code from the provided URI string onto the
		 * E-Ink display. The QR code is centered on the screen.
		 *
		 * @param uri_string The URI string to encode in the QR code (e.g., WiFi provisioning URL)
		 *                   Must not be NULL.
		 */
		static void show_epd_qr_code(const char *uri_string);
	private:
		/**
		 * @brief Callback function for rendering QR code onto EPD
		 *
		 * This function is invoked by the QR code generation library to render
		 * the generated QR code directly onto the E-Paper display.
		 *
		 * @param qrcode Handle to the generated QR code
		 */
		static void _epd_qrcode_display_cb(esp_qrcode_handle_t qrcode);
};

#ifndef WIFI_QRCODE_H
#define WIFI_QRCODE_H

#include <stdbool.h>

#define TAG_WIFI_QRCODE "WIFI_QRCODE"

/* Display dimensions */
#define SCREEN_HALF_WIDTH	600
#define SCREEN_WIDTH			1200
#define SCREEN_HEIGHT		1600

/* QR code scaling factor */
#define QR_SCALE          8

/**
 * @brief Display a WiFi QR code on the E-Paper display
 *
 * Generates and renders a QR code from the provided URI string onto the
 * E-Ink display. The QR code is centered on the screen.
 *
 * @param uri_string The URI string to encode in the QR code (e.g., WiFi provisioning URL)
 *                   Must not be NULL.
 */
void show_epd_qr_code(const char *uri_string);

#endif /* WIFI_QRCODE_H */
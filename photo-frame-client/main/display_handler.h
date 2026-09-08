#ifndef DISPLAY_HANDLER_H
#define DISPLAY_HANDLER_H

#define TAG_DISPLAY_HANDLER "DISPLAY_HANDLER"

/**
 * @brief Initialize the e-ink display
 *
 * Performs hardware initialization including GPIO setup, SPI initialization,
 * power management, and EPD initialization.
 */
void init_display(void);

/**
 * @brief Set the display color
 *
 * @param colorSelect The color to display
 */
void clearScreen();

#endif /* DISPLAY_HANDLER_H */

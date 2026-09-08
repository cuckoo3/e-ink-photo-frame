#include <string.h>
#include <stdbool.h>
#include "nvs_flash.h"

// --- Good-Display EPD related headers ---
#include "dpp_enrollee.h"
#include "wifi_qrcode.h"
#include "display_handler.h"

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

idf_component_register(SRCS "dpp_enrollee_main.c"
							"GDEP133C02.c"
                            "comm.c"
                            "display_handler.c"
                            "wifi_qrcode.c"
                    PRIV_REQUIRES esp_driver_gpio esp_driver_spi esp_wifi wpa_supplicant nvs_flash qrcode freertos
                    INCLUDE_DIRS ".")

component_compile_options("-include" "stdbool.h")
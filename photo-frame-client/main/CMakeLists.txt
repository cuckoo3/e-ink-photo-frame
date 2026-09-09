idf_component_register(SRCS "main.c"
							"dpp_enrollee.c"
							"GDEP133C02.c"
                            "comm.c"
                            "display_handler.c"
                            "wifi_qrcode.c"
                            "server_comm.c"
                    PRIV_REQUIRES
                    			esp_driver_gpio
                    			esp_driver_spi
                    			esp_wifi
                    			esp_event
                    			esp_http_client
                    			nvs_flash
                    			qrcode
                    			json
                    			freertos
                    INCLUDE_DIRS "."
                    REQUIRES
				        wpa_supplicant
				)

component_compile_options("-include" "stdbool.h")
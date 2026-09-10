idf_component_register(SRCS "main.cpp"
							"dpp_enrollee.cpp"
							"display_handler.cpp"
							"wifi_qrcode.cpp"
							"server_comm.cpp"
							"GDEP133C02.c"
							"comm.c"
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
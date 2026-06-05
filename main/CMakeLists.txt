idf_component_register(SRCS "main.c" "lcd.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_wifi esp_http_client esp_netif nvs_flash json freertos esp_rom mbedtls mqtt
                    PRIV_REQUIRES esp_driver_gpio)

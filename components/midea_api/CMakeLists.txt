idf_component_register(
    SRCS "midea_api.c"
    INCLUDE_DIRS "include" "../../include"
    REQUIRES lwip mbedtls nvs_flash esp_timer
)

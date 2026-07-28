idf_component_register(
    SRCS
        "bt_link.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_driver_uart
)
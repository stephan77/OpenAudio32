idf_component_register(
    SRCS
        "bt_receiver.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        esp_driver_uart
        esp_driver_gpio
        freertos
)
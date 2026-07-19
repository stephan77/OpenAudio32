idf_component_register(
    SRCS
        "station_manager.c"

    INCLUDE_DIRS
        "."

    REQUIRES
        nvs_flash
)
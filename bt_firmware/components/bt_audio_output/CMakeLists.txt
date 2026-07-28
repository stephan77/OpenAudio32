idf_component_register(
    SRCS
        "bt_audio_output.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_driver_i2s
)
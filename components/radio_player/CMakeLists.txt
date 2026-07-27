idf_component_register(
    SRCS
        "radio_player.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        streamer
        stations
        freertos
        audio
        media_manager
)
idf_component_register(
    SRCS
        "main.c"

    INCLUDE_DIRS
        "."

    REQUIRES
        json
        audio
        network
        streamer
        web
        stations
        settings
        nvs_flash
        media_manager
        airplay_player
        bt_receiver
)
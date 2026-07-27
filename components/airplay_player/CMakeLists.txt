idf_component_register(
    SRCS
        "airplay_player.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        freertos
        log
        media_manager
        airplay_core
)
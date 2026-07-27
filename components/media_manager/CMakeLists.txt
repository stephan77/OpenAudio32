idf_component_register(
    SRCS
        "media_manager.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        freertos
        log
)
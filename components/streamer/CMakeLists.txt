idf_component_register(

    SRCS

        "streamer.c"

    INCLUDE_DIRS

        "."

    REQUIRES

        audio

        mp3

        esp_http_client

        heap
        mbedtls
)
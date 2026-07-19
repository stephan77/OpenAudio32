set(CSPOT_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../external/cspot/cspot"
)

# Nur die für Spotify Connect benötigten Bell-Funktionen aktivieren.
#
# Wichtig:
# - Vorbis wird für Spotify benötigt.
# - BellHTTPServer wird zunächst für Spotify-Zeroconf benötigt.
# - Die eingebauten Audio-Sinks brauchen wir nicht.
# - MQTT, MP3, AAC und Opus brauchen wir nicht.
set(BELL_DISABLE_MQTT ON CACHE BOOL "" FORCE)
set(BELL_DISABLE_SINKS ON CACHE BOOL "" FORCE)

set(BELL_CODEC_AAC OFF CACHE BOOL "" FORCE)
set(BELL_CODEC_MP3 ON CACHE BOOL "" FORCE)
set(BELL_CODEC_VORBIS ON CACHE BOOL "" FORCE)
set(BELL_CODEC_OPUS OFF CACHE BOOL "" FORCE)
set(BELL_CODEC_ALAC OFF CACHE BOOL "" FORCE)

set(BELL_DISABLE_FMT ON CACHE BOOL "" FORCE)
set(BELL_DISABLE_REGEX ON CACHE BOOL "" FORCE)

# Noch nicht deaktivieren:
# cspot verwendet BellHTTPServer für /spotify_info.
set(BELL_DISABLE_WEBSERVER ON CACHE BOOL "" FORCE)

set(BELL_ONLY_CJSON OFF CACHE BOOL "" FORCE)

idf_component_register(
    SRCS
        "spotify_player.cpp"
        "spotify_audio_sink.cpp"
        "spotify_credentials.cpp"

    INCLUDE_DIRS
        "include"

    REQUIRES
        audio
        radio_player
        settings
        nvs_flash
        esp_http_server
        esp_netif
        esp_wifi
        espressif__mdns
        mbedtls
        pthread
        lwip
)

add_subdirectory(
    "${CSPOT_ROOT}"
    "${CMAKE_CURRENT_BINARY_DIR}/cspot"
)

target_link_libraries(
    ${COMPONENT_LIB}
    PUBLIC
        cspot
)

target_compile_features(
    ${COMPONENT_LIB}
    PUBLIC
        cxx_std_20
)

target_compile_options(
    ${COMPONENT_LIB}
    PRIVATE
        -Wno-error
        -Wno-deprecated-declarations
)
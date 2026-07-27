set(AIRPLAY_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../external/airplay-esp32/main"
)

set(AIRPLAY_SOURCES
    "${AIRPLAY_ROOT}/alac_magic_cookie.c"

    # Audio
    "${AIRPLAY_ROOT}/audio/audio_receiver.c"
    "${AIRPLAY_ROOT}/audio/audio_stream.c"
    "${AIRPLAY_ROOT}/audio/audio_stream_realtime.c"
    "${AIRPLAY_ROOT}/audio/audio_stream_buffered.c"
    "${AIRPLAY_ROOT}/audio/audio_buffer.c"
    "${AIRPLAY_ROOT}/audio/audio_timing.c"
    "${AIRPLAY_ROOT}/audio/audio_resample.c"

    # Netzwerk / Timing / mDNS
    "${AIRPLAY_ROOT}/network/socket_utils.c"
    "${AIRPLAY_ROOT}/network/ptp_clock.c"
    "${AIRPLAY_ROOT}/network/ntp_clock.c"
    "${AIRPLAY_ROOT}/network/mdns_airplay.c"

    # RTSP
    "${AIRPLAY_ROOT}/rtsp/rtsp_server.c"
    "${AIRPLAY_ROOT}/rtsp/rtsp_conn.c"
    "${AIRPLAY_ROOT}/rtsp/rtsp_message.c"
    "${AIRPLAY_ROOT}/rtsp/rtsp_handlers.c"
    "${AIRPLAY_ROOT}/rtsp/rtsp_crypto.c"
    "${AIRPLAY_ROOT}/rtsp/rtsp_rsa.c"
    "${AIRPLAY_ROOT}/rtsp/rtsp_events.c"
    "${AIRPLAY_ROOT}/rtsp/rtsp_fairplay.c"

    # Property Lists
    "${AIRPLAY_ROOT}/plist/plist_xml.c"
    "${AIRPLAY_ROOT}/plist/bplist_parser.c"
    "${AIRPLAY_ROOT}/plist/bplist_builder.c"
    "${AIRPLAY_ROOT}/plist/base64.c"

    # HomeKit Accessory Protocol / AirPlay-Pairing
    "${AIRPLAY_ROOT}/hap/hap.c"
    "${AIRPLAY_ROOT}/hap/hap_crypto.c"
    "${AIRPLAY_ROOT}/hap/hap_pair_setup.c"
    "${AIRPLAY_ROOT}/hap/hap_pair_verify.c"
    "${AIRPLAY_ROOT}/hap/srp.c"
    "${AIRPLAY_ROOT}/hap/tlv8.c"

    # Fernsteuerung durch iPhone/iTunes
    "${AIRPLAY_ROOT}/dacp_client.c"
)

idf_component_register(
    SRCS
        "airplay_core.c"
        "airplay_pcm_task.c"
        "airplay_audio_decoder.c"
        "airplay_audio_crypto.c"
        "airplay_platform_compat.c"
        ${AIRPLAY_SOURCES}

    INCLUDE_DIRS
        "include"
        "${AIRPLAY_ROOT}"
        "${AIRPLAY_ROOT}/audio"
        "${AIRPLAY_ROOT}/rtsp"
        "${AIRPLAY_ROOT}/plist"
        "${AIRPLAY_ROOT}/hap"
        "${AIRPLAY_ROOT}/network"

    REQUIRES
        airplay_adapter
        audio
        audio-resampler
        esp_audio_codec
        espressif__mdns
        espressif__libsodium
        freertos
        log
        esp_timer
        esp_event
        esp_netif
        esp_wifi
        esp_ringbuf
        esp_http_client
        mbedtls
        lwip
        nvs_flash
        media_manager
        radio_player
)

target_compile_definitions(
    ${COMPONENT_LIB}
    PRIVATE
        CONFIG_AIRPLAY_FORCE_V1=1
        CONFIG_OUTPUT_SAMPLE_RATE_HZ=44100
)

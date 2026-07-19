idf_component_register(
    SRCS
        "web_server.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        esp_http_server
        esp_wifi
        esp_timer
        audio
        stations
        radio_player
        settings
        spotify

    EMBED_TXTFILES
        "index.html"

        "css/app.css"
        "css/player.css"
        "css/stations.css"
        "css/system.css"


        "js/api.js"
        "js/player.js"
        "js/stations.js"
        "js/app.js"
        "js/system.js"
)
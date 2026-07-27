idf_component_register(
    SRCS
        "web_server.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        esp_http_server
        esp_wifi
        esp_timer
        esp_system
        audio
        stations
        radio_player
        settings
        json
        network

    EMBED_TXTFILES
        "index.html"

        "css/app.css"
        "css/player.css"
        "css/stations.css"
        "css/system.css"
        "css/audio.css"
        "css/wifi.css"
        
        "js/api.js"
        "js/player.js"
        "js/stations.js"
        "js/system.js"
        "js/audio.js"
        "js/wifi.js"
        "js/app.js"

        "pages/player.html"
        "pages/stations.html"
        "pages/audio.html"
        "pages/wifi.html"
        "pages/system.html"
        "pages/station-modal.html"
)
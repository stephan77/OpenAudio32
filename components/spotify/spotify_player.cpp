#include "spotify_player.h"
#include "radio_player.h"
#include <atomic>
#include <cctype>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "LoginBlob.h"
#include "CSpotContext.h"
#include "SpircHandler.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "mdns.h"

#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include <vector>

#include <variant>

#include "audio.h"
#include "TrackPlayer.h"
#include <cstdint>

#include <stdexcept>

#include <string_view>

static const char *TAG = "spotify_player";

static constexpr const char *SPOTIFY_DEVICE_NAME =
    "OpenAudio32";

static constexpr const char *SPOTIFY_HOSTNAME =
    "openaudio32";

static constexpr uint16_t SPOTIFY_ZEROCONF_PORT =
    80;

static constexpr size_t MAX_ZEROCONF_BODY_SIZE =
    16 * 1024;

static std::atomic<bool> running{false};

static std::atomic<bool> credentials_received{
    false
};

static std::atomic<spotify_player_state_t> state{
    SPOTIFY_PLAYER_STATE_DISABLED
};

static TaskHandle_t spotify_task_handle =
    nullptr;

static std::shared_ptr<cspot::LoginBlob>
    login_blob;
static std::shared_ptr<cspot::Context> spotify_context;
static std::shared_ptr<cspot::SpircHandler> spirc_handler;
static std::mutex login_blob_mutex;


/*
 * Wandelt eine einzelne hexadezimale Ziffer um.
 */
static int hex_value(char character)
{
    if (character >= '0' &&
        character <= '9') {

        return character - '0';
    }

    character = static_cast<char>(
        std::tolower(
            static_cast<unsigned char>(
                character
            )
        )
    );

    if (character >= 'a' &&
        character <= 'f') {

        return character - 'a' + 10;
    }

    return -1;
}


/*
 * Dekodiert application/x-www-form-urlencoded.
 *
 * Dabei werden:
 * +       zu Leerzeichen
 * %xx     zu den entsprechenden Bytes
 */
static std::string url_decode(
    const std::string &encoded
)
{
    std::string decoded;

    decoded.reserve(
        encoded.size()
    );

    for (size_t index = 0;
         index < encoded.size();
         ++index) {

        const char character =
            encoded[index];

        if (character == '+') {
            decoded.push_back(' ');
            continue;
        }

        if (character == '%' &&
            index + 2 < encoded.size()) {

            const int high =
                hex_value(
                    encoded[index + 1]
                );

            const int low =
                hex_value(
                    encoded[index + 2]
                );

            if (high >= 0 &&
                low >= 0) {

                decoded.push_back(
                    static_cast<char>(
                        (high << 4) | low
                    )
                );

                index += 2;
                continue;
            }
        }

        decoded.push_back(
            character
        );
    }

    return decoded;
}


/*
 * Zerlegt einen URL-kodierten POST-Body in Schlüssel und Werte.
 */
static std::map<std::string, std::string>
parse_form_urlencoded(
    const std::string &body
)
{
    std::map<std::string, std::string>
        parameters;

    size_t position = 0;

    while (position < body.size()) {
        const size_t separator =
            body.find(
                '&',
                position
            );

        const size_t field_end =
            separator == std::string::npos
                ? body.size()
                : separator;

        const std::string field =
            body.substr(
                position,
                field_end - position
            );

        const size_t equals =
            field.find('=');

        if (equals == std::string::npos) {
            parameters[
                url_decode(field)
            ] = "";
        } else {
            const std::string key =
                url_decode(
                    field.substr(
                        0,
                        equals
                    )
                );

            const std::string value =
                url_decode(
                    field.substr(
                        equals + 1
                    )
                );

            parameters[key] =
                value;
        }

        if (separator == std::string::npos) {
            break;
        }

        position =
            separator + 1;
    }

    return parameters;
}


/*
 * Liest den vollständigen HTTP-POST-Body.
 */
static esp_err_t receive_http_body(
    httpd_req_t *request,
    std::string &body
)
{
    if (request == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    if (request->content_len <= 0) {
        body.clear();
        return ESP_OK;
    }

    if (static_cast<size_t>(
            request->content_len
        ) > MAX_ZEROCONF_BODY_SIZE) {

        ESP_LOGE(
            TAG,
            "Spotify-Zeroconf-POST ist zu gross: %d Bytes",
            request->content_len
        );

        httpd_resp_send_err(
            request,
            HTTPD_413_CONTENT_TOO_LARGE,
            "Request body too large"
        );

        return ESP_ERR_INVALID_SIZE;
    }

    body.resize(
        static_cast<size_t>(
            request->content_len
        )
    );

    size_t received_total = 0;

    while (received_total < body.size()) {
        const int received =
            httpd_req_recv(
                request,
                body.data() + received_total,
                body.size() - received_total
            );

        if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }

        if (received <= 0) {
            ESP_LOGE(
                TAG,
                "Spotify-Zeroconf-POST konnte nicht gelesen werden"
            );

            return ESP_FAIL;
        }

        received_total +=
            static_cast<size_t>(
                received
            );
    }

    return ESP_OK;
}


/*
 * GET /spotify_info
 *
 * Die Spotify-App fragt hier Geräteinformationen
 * und den öffentlichen Zeroconf-Schlüssel ab.
 */
extern "C" esp_err_t
spotify_player_zeroconf_get_handler(
    httpd_req_t *request
)
{
    std::shared_ptr<cspot::LoginBlob>
        current_blob;

    {
        std::lock_guard<std::mutex>
            lock(login_blob_mutex);

        current_blob =
            login_blob;
    }

    if (!current_blob) {
        ESP_LOGW(
            TAG,
            "GET /spotify_info vor Spotify-Initialisierung"
        );

        httpd_resp_set_status(
    request,
    "503 Service Unavailable"
);

httpd_resp_set_type(
    request,
    "text/plain"
);

httpd_resp_send(
    request,
    "Spotify is not ready",
    HTTPD_RESP_USE_STRLEN
);

return ESP_ERR_INVALID_STATE;
    }

    try {
        const std::string response =
            current_blob->
                buildZeroconfInfo();

        httpd_resp_set_type(
            request,
            "application/json"
        );

        httpd_resp_set_hdr(
            request,
            "Cache-Control",
            "no-store"
        );

        httpd_resp_set_hdr(
            request,
            "Access-Control-Allow-Origin",
            "*"
        );

        return httpd_resp_send(
            request,
            response.c_str(),
            static_cast<ssize_t>(
                response.size()
            )
        );
    } catch (const std::exception &exception) {
        ESP_LOGE(
            TAG,
            "Spotify-Zeroconf-GET fehlgeschlagen: %s",
            exception.what()
        );
    } catch (...) {
        ESP_LOGE(
            TAG,
            "Spotify-Zeroconf-GET unbekannter Fehler"
        );
    }

    httpd_resp_send_err(
        request,
        HTTPD_500_INTERNAL_SERVER_ERROR,
        "Spotify Zeroconf failed"
    );

    return ESP_FAIL;
}


/*
 * POST /spotify_info
 *
 * Hier sendet die Spotify-App den verschlüsselten Login-Blob.
 */
extern "C" esp_err_t
spotify_player_zeroconf_post_handler(
    httpd_req_t *request
)
{
    std::shared_ptr<cspot::LoginBlob>
        current_blob;

    {
        std::lock_guard<std::mutex>
            lock(login_blob_mutex);

        current_blob =
            login_blob;
    }

    if (!current_blob) {
        ESP_LOGW(
            TAG,
            "POST /spotify_info vor Spotify-Initialisierung"
        );

        httpd_resp_set_status(
    request,
    "503 Service Unavailable"
);

httpd_resp_set_type(
    request,
    "text/plain; charset=utf-8"
);

httpd_resp_send(
    request,
    "Spotify is not ready",
    HTTPD_RESP_USE_STRLEN
);

return ESP_ERR_INVALID_STATE;
    }

    std::string body;

    const esp_err_t receive_result =
        receive_http_body(
            request,
            body
        );

    if (receive_result != ESP_OK) {
        return receive_result;
    }

    try {
        auto query_parameters =
            parse_form_urlencoded(
                body
            );

        if (query_parameters.empty()) {
            ESP_LOGW(
                TAG,
                "Spotify-Zeroconf-POST enthaelt keine Parameter"
            );
                } else {
            const spotify_player_state_t current_state =
                state.load();

            const bool spotify_session_active =
                current_state == SPOTIFY_PLAYER_STATE_CONNECTING ||
                current_state == SPOTIFY_PLAYER_STATE_READY ||
                current_state == SPOTIFY_PLAYER_STATE_PLAYING ||
                current_state == SPOTIFY_PLAYER_STATE_PAUSED;

            if (spotify_session_active) {
                ESP_LOGD(
                    TAG,
                    "Spotify-Zeroconf-POST ignoriert, Sitzung ist bereits aktiv"
                );
            } else {
                current_blob->
                    loadZeroconfQuery(
                        query_parameters
                    );

                credentials_received.store(
                    true
                );

                state.store(
                    SPOTIFY_PLAYER_STATE_CONNECTING
                );

                ESP_LOGI(
                    TAG,
                    "Spotify-Anmeldedaten ueber Zeroconf empfangen"
                );
            }
        }
    } catch (const std::exception &exception) {
        ESP_LOGE(
            TAG,
            "Spotify-Zeroconf-POST fehlgeschlagen: %s",
            exception.what()
        );

        httpd_resp_send_err(
            request,
            HTTPD_400_BAD_REQUEST,
            "Invalid Spotify credentials"
        );

        return ESP_FAIL;
    } catch (...) {
        ESP_LOGE(
            TAG,
            "Spotify-Zeroconf-POST unbekannter Fehler"
        );

        httpd_resp_send_err(
            request,
            HTTPD_400_BAD_REQUEST,
            "Invalid Spotify credentials"
        );

        return ESP_FAIL;
    }

    static const char response[] =
        "{"
            "\"status\":101,"
            "\"spotifyError\":0,"
            "\"statusString\":\"ERROR-OK\""
        "}";

    httpd_resp_set_type(
        request,
        "application/json"
    );

    httpd_resp_set_hdr(
        request,
        "Cache-Control",
        "no-store"
    );

    httpd_resp_set_hdr(
        request,
        "Access-Control-Allow-Origin",
        "*"
    );

    return httpd_resp_send(
        request,
        response,
        HTTPD_RESP_USE_STRLEN
    );
}


/*
 * Spotify-Hintergrundtask.
 *
 * Der HTTP-Server selbst läuft im Web-Modul.
 * Dieser Task verwaltet nur LoginBlob, mDNS und später cspot.
 */
static void spotify_task(
    void *parameter
)
{
    (void)parameter;

    ESP_LOGI(
        TAG,
        "Starte Spotify-Zeroconf fuer Geraet: %s",
        SPOTIFY_DEVICE_NAME
    );

    {
        std::lock_guard<std::mutex>
            lock(login_blob_mutex);

        login_blob =
            std::make_shared<
                cspot::LoginBlob
            >(
                SPOTIFY_DEVICE_NAME
            );
    }

    credentials_received.store(
        false
    );

    state.store(
        SPOTIFY_PLAYER_STATE_WAITING_FOR_LOGIN
    );

    const esp_err_t mdns_result =
        mdns_init();

    if (mdns_result != ESP_OK &&
        mdns_result != ESP_ERR_INVALID_STATE) {

        ESP_LOGE(
            TAG,
            "mDNS konnte nicht initialisiert werden: %s",
            esp_err_to_name(
                mdns_result
            )
        );

        state.store(
            SPOTIFY_PLAYER_STATE_ERROR
        );

        running.store(false);

        {
            std::lock_guard<std::mutex>
                lock(login_blob_mutex);

            login_blob.reset();
        }

        spotify_task_handle =
            nullptr;

        vTaskDelete(nullptr);
        return;
    }

    const esp_err_t hostname_result =
        mdns_hostname_set(
            SPOTIFY_HOSTNAME
        );

    if (hostname_result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "mDNS-Hostname konnte nicht gesetzt werden: %s",
            esp_err_to_name(
                hostname_result
            )
        );
    }

    mdns_txt_item_t txt_records[] = {
        {
            .key = "VERSION",
            .value = "1.0"
        },
        {
            .key = "CPath",
            .value = "/spotify_info"
        },
        {
            .key = "Stack",
            .value = "SP"
        }
    };

    const esp_err_t service_result =
        mdns_service_add(
            SPOTIFY_DEVICE_NAME,
            "_spotify-connect",
            "_tcp",
            SPOTIFY_ZEROCONF_PORT,
            txt_records,
            sizeof(txt_records) /
                sizeof(txt_records[0])
        );

    if (service_result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Spotify-mDNS-Dienst konnte nicht registriert werden: %s",
            esp_err_to_name(
                service_result
            )
        );

        state.store(
            SPOTIFY_PLAYER_STATE_ERROR
        );

        running.store(false);

        {
            std::lock_guard<std::mutex>
                lock(login_blob_mutex);

            login_blob.reset();
        }

        spotify_task_handle =
            nullptr;

        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(
        TAG,
        "Spotify Connect sichtbar als \"%s\"",
        SPOTIFY_DEVICE_NAME
    );

    ESP_LOGI(
        TAG,
        "Spotify-Zeroconf erreichbar unter http://%s.local/spotify_info",
        SPOTIFY_HOSTNAME
    );

bool login_started = false;

while (running.load()) {
    if (credentials_received.load() &&
        !login_started) {

        login_started = true;

        state.store(
            SPOTIFY_PLAYER_STATE_CONNECTING
        );
ESP_LOGI(
    TAG,
    "Stoppe Webradio vor Spotify-Anmeldung"
);

const esp_err_t radio_stop_result =
    radio_player_stop(3000);

if (radio_stop_result != ESP_OK) {
    ESP_LOGW(
        TAG,
        "Webradio konnte nicht sauber gestoppt werden: %s",
        esp_err_to_name(radio_stop_result)
    );
}

vTaskDelay(
    pdMS_TO_TICKS(300)
);
        ESP_LOGI(
            TAG,
            "Erstelle Spotify-Kontext"
        );

        try {
            spotify_context =
                cspot::Context::createFromBlob(
                    login_blob
                );

            if (!spotify_context ||
                !spotify_context->session) {

                ESP_LOGE(
                    TAG,
                    "Spotify-Kontext oder Session ist ungueltig"
                );

                state.store(
                    SPOTIFY_PLAYER_STATE_ERROR
                );

                running.store(false);
                break;
            }

            ESP_LOGI(
                TAG,
                "Verbinde mit Spotify Access Point"
            );

            spotify_context
                ->session
                ->connectWithRandomAp();

            ESP_LOGI(
                TAG,
                "Authentifiziere bei Spotify"
            );

            const auto auth_token =
                spotify_context
                    ->session
                    ->authenticate(
                        login_blob
                    );

            if (auth_token.empty()) {
                ESP_LOGE(
                    TAG,
                    "Spotify-Authentifizierung fehlgeschlagen"
                );

                state.store(
                    SPOTIFY_PLAYER_STATE_ERROR
                );

                running.store(false);
                break;
            }

            ESP_LOGI(
                TAG,
                "Spotify-Authentifizierung erfolgreich"
            );

            credentials_received.store(
                false
            );

            spotify_context
                ->session
                ->startTask();

spirc_handler =
    std::make_shared<cspot::SpircHandler>(
        spotify_context
    );

if (!spirc_handler) {
    throw std::runtime_error(
        "SpircHandler konnte nicht erstellt werden"
    );
}

auto track_player =
    spirc_handler->getTrackPlayer();

if (!track_player) {
    throw std::runtime_error(
        "Spotify TrackPlayer ist nicht verfuegbar"
    );
}

track_player->setDataCallback(
    [](
        uint8_t *pcm_data,
        size_t pcm_bytes,
        std::string_view format
    ) -> size_t {
        if (pcm_data == nullptr ||
            pcm_bytes == 0) {

            return 0;
        }

        constexpr size_t bytes_per_frame =
            sizeof(int16_t) * 2;

        const size_t frame_count =
            pcm_bytes / bytes_per_frame;

        if (frame_count == 0) {
            return 0;
        }

        const esp_err_t result =
            audio_submit(
                reinterpret_cast<const int16_t *>(
                    pcm_data
                ),
                frame_count,
                100
            );

        if (result != ESP_OK) {
            ESP_LOGW(
                TAG,
                "Spotify-PCM konnte nicht ausgegeben werden: %s, Bytes=%u, Format=%.*s",
                esp_err_to_name(result),
                static_cast<unsigned>(pcm_bytes),
                static_cast<int>(format.size()),
                format.data()
            );

            return 0;
        }

        return frame_count * bytes_per_frame;
    }
);

spirc_handler->setEventHandler(
    [](
        std::unique_ptr<
            cspot::SpircHandler::Event
        > event
    ) {
        if (!event) {
            return;
        }

        ESP_LOGI(
            TAG,
            "Spotify-Event empfangen: %d",
            static_cast<int>(
                event->eventType
            )
        );

        switch (event->eventType) {
        case cspot::SpircHandler::EventType::PLAYBACK_START:
            ESP_LOGI(
                TAG,
                "Spotify-Wiedergabe startet"
            );

            audio_set_playback_active(true);

            state.store(
                SPOTIFY_PLAYER_STATE_PLAYING
            );
            break;

        case cspot::SpircHandler::EventType::PLAY_PAUSE:
            if (std::holds_alternative<bool>(
                    event->data
                )) {

                const bool paused =
                    std::get<bool>(
                        event->data
                    );

                audio_set_playback_active(
                    !paused
                );

                state.store(
                    paused
                        ? SPOTIFY_PLAYER_STATE_PAUSED
                        : SPOTIFY_PLAYER_STATE_PLAYING
                );

                ESP_LOGI(
                    TAG,
                    "Spotify-Wiedergabe: %s",
                    paused ? "PAUSE" : "PLAY"
                );
            } else {
                ESP_LOGW(
                    TAG,
                    "PLAY_PAUSE ohne bool-Daten empfangen"
                );
            }
            break;

        case cspot::SpircHandler::EventType::DEPLETED:
            ESP_LOGI(
                TAG,
                "Spotify-Titel beendet"
            );

            audio_set_playback_active(false);

            state.store(
                SPOTIFY_PLAYER_STATE_READY
            );
            break;

        case cspot::SpircHandler::EventType::DISC:
            ESP_LOGW(
                TAG,
                "Spotify-Geraet getrennt"
            );

            audio_set_playback_active(false);

            state.store(
                SPOTIFY_PLAYER_STATE_READY
            );
            break;

        case cspot::SpircHandler::EventType::FLUSH:
            ESP_LOGI(
                TAG,
                "Spotify-Audiopuffer-Flush angefordert"
            );
            break;

        case cspot::SpircHandler::EventType::VOLUME:
            if (std::holds_alternative<int>(
                    event->data
                )) {

                const int spotify_volume =
                    std::get<int>(
                        event->data
                    );

                ESP_LOGI(
                    TAG,
                    "Spotify-Lautstaerke: %d",
                    spotify_volume
                );
            }
            break;

        case cspot::SpircHandler::EventType::TRACK_INFO:
            ESP_LOGI(
                TAG,
                "Spotify-Titelinformationen empfangen"
            );
            break;

        case cspot::SpircHandler::EventType::SEEK:
            ESP_LOGI(
                TAG,
                "Spotify-Seek"
            );
            break;

        case cspot::SpircHandler::EventType::NEXT:
            ESP_LOGI(
                TAG,
                "Spotify-Naechster Titel"
            );
            break;

        case cspot::SpircHandler::EventType::PREV:
            ESP_LOGI(
                TAG,
                "Spotify-Vorheriger Titel"
            );
            break;

        default:
            ESP_LOGI(
                TAG,
                "Spotify-Event ohne eigene Behandlung: %d",
                static_cast<int>(
                    event->eventType
                )
            );
            break;
        }
    }
);

ESP_LOGI(
    TAG,
    "Spotify TrackPlayer mit Audioausgabe verbunden"
);



state.store(
    SPOTIFY_PLAYER_STATE_READY
);

credentials_received.store(
    false
);

ESP_LOGI(
    TAG,
    "Spotify Connect ist bereit"
);
        } catch (const std::exception &exception) {
            ESP_LOGE(
                TAG,
                "Spotify-Anmeldung fehlgeschlagen: %s",
                exception.what()
            );

            state.store(
                SPOTIFY_PLAYER_STATE_ERROR
            );

            running.store(false);
            break;
        } catch (...) {
            ESP_LOGE(
                TAG,
                "Spotify-Anmeldung mit unbekanntem Fehler fehlgeschlagen"
            );

            state.store(
                SPOTIFY_PLAYER_STATE_ERROR
            );

            running.store(false);
            break;
        }
    }

if (spotify_context &&

        spotify_context->session) {

        spotify_context

            ->session

            ->handlePacket();

    }

    vTaskDelay(

        pdMS_TO_TICKS(10)

    );

}

    ESP_LOGI(
        TAG,
        "Spotify-Zeroconf wird beendet"
    );

    mdns_service_remove(
        "_spotify-connect",
        "_tcp"
    );

    {
        std::lock_guard<std::mutex>
            lock(login_blob_mutex);

        login_blob.reset();
    }

    credentials_received.store(
        false
    );

    state.store(
        SPOTIFY_PLAYER_STATE_DISABLED
    );

    spotify_task_handle =
        nullptr;

    vTaskDelete(nullptr);
}


extern "C" esp_err_t
spotify_player_init(void)
{
    running.store(false);

    credentials_received.store(
        false
    );

    state.store(
        SPOTIFY_PLAYER_STATE_DISABLED
    );

    spotify_task_handle =
        nullptr;

    {
        std::lock_guard<std::mutex>
            lock(login_blob_mutex);
spirc_handler.reset();
spotify_context.reset();
        login_blob.reset();
    }

    ESP_LOGI(
        TAG,
        "Spotify-Player initialisiert"
    );

    return ESP_OK;
}


extern "C" esp_err_t
spotify_player_start(void)
{
    if (running.load()) {
        ESP_LOGW(
            TAG,
            "Spotify-Player laeuft bereits"
        );

        return ESP_OK;
    }

    running.store(true);

    credentials_received.store(
        false
    );

    state.store(
        SPOTIFY_PLAYER_STATE_STARTING
    );

    const BaseType_t task_result =
        xTaskCreatePinnedToCoreWithCaps(
            spotify_task,
            "spotify",
            20 * 1024,
            nullptr,
            5,
            &spotify_task_handle,
            1,
            MALLOC_CAP_INTERNAL |
                MALLOC_CAP_8BIT
        );

    ESP_LOGI(
        TAG,
        "Spotify-Task-Erstellung Ergebnis=%d",
        static_cast<int>(
            task_result
        )
    );

    if (task_result != pdPASS) {
        ESP_LOGE(
            TAG,
            "Spotify-Task konnte nicht erstellt werden"
        );

        spotify_task_handle =
            nullptr;

        running.store(false);

        state.store(
            SPOTIFY_PLAYER_STATE_ERROR
        );

        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(
        TAG,
        "Spotify-Player wird gestartet"
    );

    return ESP_OK;
}


extern "C" esp_err_t
spotify_player_stop(void)
{
    if (!running.load()) {
        return ESP_OK;
    }

    running.store(false);

    ESP_LOGI(
        TAG,
        "Spotify-Player wird gestoppt"
    );

    return ESP_OK;
}


extern "C" bool
spotify_player_is_running(void)
{
    return running.load();
}


extern "C" spotify_player_state_t
spotify_player_get_state(void)
{
    return state.load();
}


extern "C" const char *
spotify_player_get_state_name(void)
{
    switch (state.load()) {
    case SPOTIFY_PLAYER_STATE_DISABLED:
        return "Deaktiviert";

    case SPOTIFY_PLAYER_STATE_STARTING:
        return "Wird gestartet";

    case SPOTIFY_PLAYER_STATE_WAITING_FOR_LOGIN:
        return "Wartet auf Spotify";

    case SPOTIFY_PLAYER_STATE_CONNECTING:
        return "Verbindet";

    case SPOTIFY_PLAYER_STATE_READY:
        return "Bereit";

    case SPOTIFY_PLAYER_STATE_PLAYING:
        return "Wiedergabe";

    case SPOTIFY_PLAYER_STATE_PAUSED:
        return "Pausiert";

    case SPOTIFY_PLAYER_STATE_ERROR:
        return "Fehler";

    default:
        return "Unbekannt";
    }
}
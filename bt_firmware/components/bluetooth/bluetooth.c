#include "bluetooth.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_a2dp_api.h"
#include "esp_bt.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_err.h"
#include "esp_gap_bt_api.h"
#include "esp_log.h"
#include "esp_avrc_api.h"
#include "bt_link.h"
#include "bt_audio_output.h"
#include <string.h>

static const char *TAG = "bluetooth";
static uint8_t s_avrc_transaction_label = 0;
static uint8_t s_absolute_volume = 64;

static uint8_t next_transaction_label(void)
{
    const uint8_t label =
        s_avrc_transaction_label;

    s_avrc_transaction_label =
        (uint8_t)(
            (s_avrc_transaction_label + 1U) & 0x0FU
        );

    return label;
}

static const char *connection_state_to_string(
    esp_a2d_connection_state_t state
)
{
    switch (state) {
    case ESP_A2D_CONNECTION_STATE_DISCONNECTED:
        return "getrennt";

    case ESP_A2D_CONNECTION_STATE_CONNECTING:
        return "verbindet";

    case ESP_A2D_CONNECTION_STATE_CONNECTED:
        return "verbunden";

    case ESP_A2D_CONNECTION_STATE_DISCONNECTING:
        return "trennt";

    default:
        return "unbekannt";
    }
}

static const char *audio_state_to_string(
    esp_a2d_audio_state_t state
)
{
    switch (state) {
    case ESP_A2D_AUDIO_STATE_SUSPEND:
        return "pausiert/gestoppt";

    case ESP_A2D_AUDIO_STATE_STARTED:
        return "gestartet";

    default:
        return "unbekannt";
    }
}

static void log_bluetooth_address(
    const char *prefix,
    const esp_bd_addr_t address
)
{
    ESP_LOGI(
        TAG,
        "%s %02x:%02x:%02x:%02x:%02x:%02x",
        prefix,
        address[0],
        address[1],
        address[2],
        address[3],
        address[4],
        address[5]
    );
}

static void gap_callback(
    esp_bt_gap_cb_event_t event,
    esp_bt_gap_cb_param_t *param
)
{
    if (param == NULL) {
        return;
    }

    switch (event) {
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(
                TAG,
                "Bluetooth-Pairing erfolgreich: %s",
                param->auth_cmpl.device_name
            );

            log_bluetooth_address(
                "Gekoppeltes Gerät:",
                param->auth_cmpl.bda
            );
        } else {
            ESP_LOGW(
                TAG,
                "Bluetooth-Pairing fehlgeschlagen, Status=%d",
                param->auth_cmpl.stat
            );
        }
        break;

    case ESP_BT_GAP_PIN_REQ_EVT: {
        esp_bt_pin_code_t pin_code = {
            '0',
            '0',
            '0',
            '0'
        };

        ESP_LOGI(
            TAG,
            "Legacy-PIN angefordert, verwende 0000"
        );

        esp_bt_gap_pin_reply(
            param->pin_req.bda,
            true,
            4,
            pin_code
        );
        break;
    }

    case ESP_BT_GAP_CFM_REQ_EVT:
        ESP_LOGI(
            TAG,
            "Pairing-Bestätigung: %06lu",
            (unsigned long)param->cfm_req.num_val
        );

        esp_bt_gap_ssp_confirm_reply(
            param->cfm_req.bda,
            true
        );
        break;

    case ESP_BT_GAP_MODE_CHG_EVT:
        ESP_LOGI(
            TAG,
            "Bluetooth-Modus geändert: mode=%d",
            param->mode_chg.mode
        );
        break;

    default:
        ESP_LOGD(
            TAG,
            "GAP-Ereignis: %d",
            event
        );
        break;
    }
}

static void a2dp_event_callback(
    esp_a2d_cb_event_t event,
    esp_a2d_cb_param_t *param
)
{
    if (param == NULL) {
        return;
    }

    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT:
        ESP_LOGI(
            TAG,
            "A2DP-Verbindung: %s",
            connection_state_to_string(
                param->conn_stat.state
            )
        );

        log_bluetooth_address(
            "A2DP-Gegenstelle:",
            param->conn_stat.remote_bda
        );

if (param->conn_stat.state ==

    ESP_A2D_CONNECTION_STATE_CONNECTED) {

    bt_link_send("BT_CONNECTED");

}

if (param->conn_stat.state ==

    ESP_A2D_CONNECTION_STATE_DISCONNECTED) {

    bt_link_send("BT_DISCONNECTED");

    esp_bt_gap_set_scan_mode(

        ESP_BT_CONNECTABLE,

        ESP_BT_GENERAL_DISCOVERABLE

    );

}
        break;



case ESP_A2D_AUDIO_STATE_EVT:
    ESP_LOGI(
        TAG,
        "A2DP-Audiostream: %s",
        audio_state_to_string(
            param->audio_stat.state
        )
    );

    if (param->audio_stat.state ==
        ESP_A2D_AUDIO_STATE_STARTED) {

        bt_link_send("BT_STREAMING");
    }

    if (param->audio_stat.state ==
        ESP_A2D_AUDIO_STATE_SUSPEND) {

        bt_link_send("BT_PAUSED");
    }
    break;

case ESP_A2D_AUDIO_CFG_EVT: {
    ESP_LOGI(
        TAG,
        "A2DP-Audiokonfiguration empfangen"
    );

    const uint8_t codec_type =
        param->audio_cfg.mcc.type;

    ESP_LOGI(
        TAG,
        "A2DP Codec-Typ: %u",
        (unsigned int)codec_type
    );

    if (codec_type == ESP_A2D_MCT_SBC) {
        const uint8_t octet0 =
            param->audio_cfg.mcc.cie.sbc[0];

        uint32_t sample_rate_hz = 44100U;

        if ((octet0 & 0x80U) != 0U) {
            sample_rate_hz = 16000U;
        } else if ((octet0 & 0x40U) != 0U) {
            sample_rate_hz = 32000U;
        } else if ((octet0 & 0x20U) != 0U) {
            sample_rate_hz = 44100U;
        } else if ((octet0 & 0x10U) != 0U) {
            sample_rate_hz = 48000U;
        }

        ESP_LOGI(
            TAG,
            "A2DP SBC Samplerate: %lu Hz",
            (unsigned long)sample_rate_hz
        );

        bt_audio_output_set_sample_rate(
            sample_rate_hz
        );
    }

    break;
}

    case ESP_A2D_PROF_STATE_EVT:
        ESP_LOGI(
            TAG,
            "A2DP-Profilstatus: %d",
            param->a2d_prof_stat.init_state
        );
        break;

    default:
        ESP_LOGD(
            TAG,
            "A2DP-Ereignis: %d",
            event
        );
        break;
    }
}

static void a2dp_audio_data_callback(
    esp_a2d_conn_hdl_t connection_handle,
    esp_a2d_audio_buff_t *audio_buffer
)
{
    static uint32_t received_buffers = 0;
    static uint64_t received_bytes = 0;
    static uint32_t write_errors = 0;

    if (audio_buffer == NULL) {
        return;
    }

    received_buffers++;

    received_bytes +=
        (uint64_t)audio_buffer->data_len;

    const esp_err_t result =
        bt_audio_output_write(
            audio_buffer->data,
            audio_buffer->data_len
        );

    if (result != ESP_OK) {
        write_errors++;

        if (write_errors <= 10U ||
            (write_errors % 100U) == 0U) {

            ESP_LOGE(
                TAG,
                "Bluetooth-Audio konnte nicht ueber I2S ausgegeben werden: %s, Fehler=%lu",
                esp_err_to_name(result),
                (unsigned long)write_errors
            );
        }
    }

    if ((received_buffers % 500U) == 0U) {
        ESP_LOGI(
            TAG,
            "Bluetooth-Audio: Handle=%u, Puffer=%lu, Bytes=%llu, "
            "Frames=%u, letzter Block=%u Byte, Schreibfehler=%lu",
            (unsigned int)connection_handle,
            (unsigned long)received_buffers,
            (unsigned long long)received_bytes,
            (unsigned int)audio_buffer->number_frame,
            (unsigned int)audio_buffer->data_len,
            (unsigned long)write_errors
        );
    }

    esp_a2d_audio_buff_free(
        audio_buffer
    );
}
static const char *avrc_key_to_string(
    esp_avrc_pt_cmd_t command
)
{
    switch (command) {
    case ESP_AVRC_PT_CMD_PLAY:
        return "Play";

    case ESP_AVRC_PT_CMD_PAUSE:
        return "Pause";

    case ESP_AVRC_PT_CMD_STOP:
        return "Stop";

    case ESP_AVRC_PT_CMD_FORWARD:
        return "Naechster Titel";

    case ESP_AVRC_PT_CMD_BACKWARD:
        return "Vorheriger Titel";

    case ESP_AVRC_PT_CMD_VOL_UP:
        return "Lautstaerke hoch";

    case ESP_AVRC_PT_CMD_VOL_DOWN:
        return "Lautstaerke runter";

    default:
        return "Unbekannt";
    }
}

static void avrc_target_callback(
    esp_avrc_tg_cb_event_t event,
    esp_avrc_tg_cb_param_t *parameter
)
{
    if (parameter == NULL) {
        return;
    }

    switch (event) {
    case ESP_AVRC_TG_CONNECTION_STATE_EVT:
        ESP_LOGI(
            TAG,
            "AVRCP Target: %s",
            parameter->conn_stat.connected
                ? "verbunden"
                : "getrennt"
        );
        break;

 case ESP_AVRC_TG_PASSTHROUGH_CMD_EVT: {
    const esp_avrc_pt_cmd_t command =
        parameter->psth_cmd.key_code;

    const unsigned int key_state =
        (unsigned int)
            parameter->psth_cmd.key_state;

    ESP_LOGI(
        TAG,
        "AVRCP-Taste: %s, Code=0x%02x, Zustand=%u",
        avrc_key_to_string(command),
        (unsigned int)command,
        key_state
    );

    /*
     * Nur beim Drücken senden, nicht noch einmal
     * beim Loslassen.
     */
    if (key_state == 0U) {
        switch (command) {
        case ESP_AVRC_PT_CMD_PLAY:
            bt_link_send("BT_KEY:PLAY");
            break;

        case ESP_AVRC_PT_CMD_PAUSE:
            bt_link_send("BT_KEY:PAUSE");
            break;

        case ESP_AVRC_PT_CMD_STOP:
            bt_link_send("BT_KEY:STOP");
            break;

        case ESP_AVRC_PT_CMD_FORWARD:
            bt_link_send("BT_KEY:NEXT");
            break;

        case ESP_AVRC_PT_CMD_BACKWARD:
            bt_link_send("BT_KEY:PREVIOUS");
            break;

        case ESP_AVRC_PT_CMD_VOL_UP:
            bt_link_send("BT_KEY:VOLUME_UP");
            break;

        case ESP_AVRC_PT_CMD_VOL_DOWN:
            bt_link_send("BT_KEY:VOLUME_DOWN");
            break;

        default:
            break;
        }
    }

    break;
}

    case ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT: {
        uint8_t volume =
            parameter->set_abs_vol.volume;

        if (volume > 127U) {
            volume = 127U;
        }

        s_absolute_volume =
            volume;

        const unsigned int percent =
            ((unsigned int)volume * 100U + 63U) /
            127U;

        ESP_LOGI(
            TAG,
            "Bluetooth-Lautstaerke empfangen: %u/127 = %u %%",
            (unsigned int)volume,
            percent
        );

char volume_message[32];

snprintf(
    volume_message,
    sizeof(volume_message),
    "BT_VOLUME:%u",
    percent
);

bt_link_send(
    volume_message
);

break;
    }

    case ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT:
        ESP_LOGI(
            TAG,
            "AVRCP Notification angefordert: Event=%u",
            (unsigned int)
                parameter->reg_ntf.event_id
        );

        if (parameter->reg_ntf.event_id ==
            ESP_AVRC_RN_VOLUME_CHANGE) {

            esp_avrc_rn_param_t notification = {0};

            notification.volume =
                s_absolute_volume;

            const esp_err_t result =
                esp_avrc_tg_send_rn_rsp(
                    ESP_AVRC_RN_VOLUME_CHANGE,
                    ESP_AVRC_RN_RSP_INTERIM,
                    &notification
                );

            if (result != ESP_OK) {
                ESP_LOGW(
                    TAG,
                    "AVRCP Lautstaerke-Interimantwort fehlgeschlagen: %s",
                    esp_err_to_name(result)
                );
            } else {
                ESP_LOGI(
                    TAG,
                    "Aktuelle Bluetooth-Lautstaerke gemeldet: %u/127",
                    (unsigned int)s_absolute_volume
                );
            }
        }
        break;

    default:
        ESP_LOGD(
            TAG,
            "AVRCP Target Event: %d",
            event
        );
        break;
    }
}

static void avrc_controller_callback(
    esp_avrc_ct_cb_event_t event,
    esp_avrc_ct_cb_param_t *parameter
)
{
    if (parameter == NULL) {
        return;
    }

    switch (event) {
case ESP_AVRC_CT_CONNECTION_STATE_EVT:
    ESP_LOGI(
        TAG,
        "AVRCP Controller: %s",
        parameter->conn_stat.connected
            ? "verbunden"
            : "getrennt"
    );

    if (parameter->conn_stat.connected) {
        esp_err_t result =
            esp_avrc_ct_send_get_rn_capabilities_cmd(
                next_transaction_label()
            );

        if (result != ESP_OK) {
            ESP_LOGW(
                TAG,
                "AVRCP Capabilities konnten nicht angefordert werden: %s",
                esp_err_to_name(result)
            );
        }

        result =
            esp_avrc_ct_send_metadata_cmd(
                next_transaction_label(),
                ESP_AVRC_MD_ATTR_TITLE |
                ESP_AVRC_MD_ATTR_ARTIST |
                ESP_AVRC_MD_ATTR_ALBUM
            );

        if (result != ESP_OK) {
            ESP_LOGW(
                TAG,
                "AVRCP Metadaten konnten nicht angefordert werden: %s",
                esp_err_to_name(result)
            );
        }
    }
    break;
case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT: {
    const esp_avrc_rn_evt_cap_mask_t *capabilities =
        &parameter->get_rn_caps_rsp.evt_set;

    ESP_LOGI(
        TAG,
        "AVRCP Notification-Capabilities empfangen"
    );

    if (esp_avrc_rn_evt_bit_mask_operation(
            ESP_AVRC_BIT_MASK_OP_TEST,
            (esp_avrc_rn_evt_cap_mask_t *)capabilities,
            ESP_AVRC_RN_PLAY_STATUS_CHANGE
        )) {

        esp_avrc_ct_send_register_notification_cmd(
            next_transaction_label(),
            ESP_AVRC_RN_PLAY_STATUS_CHANGE,
            0
        );

        ESP_LOGI(
            TAG,
            "Playstatus-Notification registriert"
        );
    }

    if (esp_avrc_rn_evt_bit_mask_operation(
            ESP_AVRC_BIT_MASK_OP_TEST,
            (esp_avrc_rn_evt_cap_mask_t *)capabilities,
            ESP_AVRC_RN_TRACK_CHANGE
        )) {

        esp_avrc_ct_send_register_notification_cmd(
            next_transaction_label(),
            ESP_AVRC_RN_TRACK_CHANGE,
            0
        );

        ESP_LOGI(
            TAG,
            "Titelwechsel-Notification registriert"
        );
    }

    if (esp_avrc_rn_evt_bit_mask_operation(
            ESP_AVRC_BIT_MASK_OP_TEST,
            (esp_avrc_rn_evt_cap_mask_t *)capabilities,
            ESP_AVRC_RN_PLAY_POS_CHANGED
        )) {

        esp_avrc_ct_send_register_notification_cmd(
            next_transaction_label(),
            ESP_AVRC_RN_PLAY_POS_CHANGED,
            5
        );

        ESP_LOGI(
            TAG,
            "Wiedergabepositions-Notification registriert"
        );
    }

    break;
}
case ESP_AVRC_CT_CHANGE_NOTIFY_EVT:
    switch (parameter->change_ntf.event_id) {
    case ESP_AVRC_RN_PLAY_STATUS_CHANGE:
        ESP_LOGI(
            TAG,
            "Wiedergabestatus geändert: %u",
            (unsigned int)
                parameter->change_ntf.event_parameter.playback
        );
char status_message[32];

snprintf(

    status_message,

    sizeof(status_message),

    "BT_PLAY_STATUS:%u",

    (unsigned int)

        parameter->change_ntf.event_parameter.playback

);

bt_link_send(

    status_message

);
        esp_avrc_ct_send_register_notification_cmd(
            next_transaction_label(),
            ESP_AVRC_RN_PLAY_STATUS_CHANGE,
            0
        );
        break;

    case ESP_AVRC_RN_TRACK_CHANGE:
        ESP_LOGI(
            TAG,
            "Titel wurde gewechselt"
        );
bt_link_send(
    "BT_TRACK_CHANGED"
);
        esp_avrc_ct_send_metadata_cmd(
            next_transaction_label(),
            ESP_AVRC_MD_ATTR_TITLE |
            ESP_AVRC_MD_ATTR_ARTIST |
            ESP_AVRC_MD_ATTR_ALBUM
        );

        esp_avrc_ct_send_register_notification_cmd(
            next_transaction_label(),
            ESP_AVRC_RN_TRACK_CHANGE,
            0
        );
        break;

    case ESP_AVRC_RN_PLAY_POS_CHANGED:
        ESP_LOGI(
            TAG,
            "Wiedergabeposition: %lu ms",
            (unsigned long)
                parameter->change_ntf.event_parameter.play_pos
        );

        esp_avrc_ct_send_register_notification_cmd(
            next_transaction_label(),
            ESP_AVRC_RN_PLAY_POS_CHANGED,
            5
        );
        break;

    default:
        ESP_LOGI(
            TAG,
            "AVRCP Statusaenderung: Event=%u",
            (unsigned int)
                parameter->change_ntf.event_id
        );
        break;
    }
    break;
case ESP_AVRC_CT_METADATA_RSP_EVT: {
    const uint8_t attribute_id =
        parameter->meta_rsp.attr_id;

    const uint8_t *attribute_text =
        parameter->meta_rsp.attr_text;

    const uint16_t attribute_length =
        parameter->meta_rsp.attr_length;

    if (attribute_text == NULL ||
        attribute_length == 0U) {

        ESP_LOGW(
            TAG,
            "Leere AVRCP-Metadaten empfangen, Attribut=%u",
            (unsigned int)attribute_id
        );

        break;
    }

    /*
     * AVRCP liefert nicht garantiert ein Nullbyte.
     * Deshalb in einen eigenen Puffer kopieren.
     */
    char text[256];

    size_t copy_length =
        attribute_length;

    if (copy_length >= sizeof(text)) {
        copy_length =
            sizeof(text) - 1U;
    }

    memcpy(
        text,
        attribute_text,
        copy_length
    );

    text[copy_length] = '\0';

    switch (attribute_id) {
    case ESP_AVRC_MD_ATTR_TITLE:
        ESP_LOGI(
            TAG,
            "Bluetooth-Titel: %s",
            text
        );

        char title_message[320];

        snprintf(
            title_message,
            sizeof(title_message),
            "BT_TITLE:%s",
            text
        );

        bt_link_send(
            title_message
        );
        break;

    case ESP_AVRC_MD_ATTR_ARTIST:
        ESP_LOGI(
            TAG,
            "Bluetooth-Interpret: %s",
            text
        );

        char artist_message[320];

        snprintf(
            artist_message,
            sizeof(artist_message),
            "BT_ARTIST:%s",
            text
        );

        bt_link_send(
            artist_message
        );
        break;

    case ESP_AVRC_MD_ATTR_ALBUM:
        ESP_LOGI(
            TAG,
            "Bluetooth-Album: %s",
            text
        );

        char album_message[320];

        snprintf(
            album_message,
            sizeof(album_message),
            "BT_ALBUM:%s",
            text
        );

        bt_link_send(
            album_message
        );
        break;

    default:
        ESP_LOGI(
            TAG,
            "Bluetooth-Metadaten: Attribut=%u, Text=%s",
            (unsigned int)attribute_id,
            text
        );
        break;
    }

    break;
}
    case ESP_AVRC_CT_REMOTE_FEATURES_EVT:
        ESP_LOGI(
            TAG,
            "AVRCP Gegenstelle erkannt, Features=0x%08lx",
            (unsigned long)
                parameter->rmt_feats.feat_mask
        );
        break;

    default:
        ESP_LOGD(
            TAG,
            "AVRCP Controller Event: %d",
            event
        );
        break;
    }
}
esp_err_t bluetooth_init(void)
{
    ESP_LOGI(
        TAG,
        "Initialisiere Bluetooth Classic und A2DP Sink"
    );

    /*
     * BLE wird nicht benötigt. Das gibt zusätzlichen RAM frei.
     */
    esp_err_t result =
        esp_bt_controller_mem_release(
            ESP_BT_MODE_BLE
        );

    if (result != ESP_OK &&
        result != ESP_ERR_INVALID_STATE) {

        ESP_LOGW(
            TAG,
            "BLE-Speicher konnte nicht freigegeben werden: %s",
            esp_err_to_name(result)
        );
    }

    esp_bt_controller_config_t controller_config =
        BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    result =
        esp_bt_controller_init(
            &controller_config
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Bluetooth-Controller konnte nicht initialisiert werden: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    result =
        esp_bt_controller_enable(
            ESP_BT_MODE_CLASSIC_BT
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Bluetooth Classic konnte nicht aktiviert werden: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    result =
        esp_bluedroid_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Bluedroid konnte nicht initialisiert werden: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    result =
        esp_bluedroid_enable();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Bluedroid konnte nicht aktiviert werden: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    result =
        esp_bt_gap_register_callback(
            gap_callback
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "GAP-Callback konnte nicht registriert werden: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    result =
        esp_a2d_register_callback(
            a2dp_event_callback
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "A2DP-Callback konnte nicht registriert werden: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    result =
        esp_a2d_sink_register_audio_data_callback(
            a2dp_audio_data_callback
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "A2DP-Audio-Callback konnte nicht registriert werden: %s",
            esp_err_to_name(result)
        );

        return result;
    }
result =
    esp_avrc_ct_register_callback(
        avrc_controller_callback
    );

if (result != ESP_OK) {
    ESP_LOGE(
        TAG,
        "AVRCP-Controller-Callback fehlgeschlagen: %s",
        esp_err_to_name(result)
    );

    return result;
}

result =
    esp_avrc_ct_init();

if (result != ESP_OK) {
    ESP_LOGE(
        TAG,
        "AVRCP Controller konnte nicht gestartet werden: %s",
        esp_err_to_name(result)
    );

    return result;
}

ESP_LOGI(
    TAG,
    "AVRCP Controller gestartet"
);

result =
    esp_avrc_tg_register_callback(
        avrc_target_callback
    );

if (result != ESP_OK) {
    ESP_LOGE(
        TAG,
        "AVRCP-Target-Callback fehlgeschlagen: %s",
        esp_err_to_name(result)
    );

    return result;
}

result =
    esp_avrc_tg_init();

if (result != ESP_OK) {
    ESP_LOGE(
        TAG,
        "AVRCP Target konnte nicht gestartet werden: %s",
        esp_err_to_name(result)
    );

    return result;
}

ESP_LOGI(
    TAG,
    "AVRCP Target gestartet"
);

esp_avrc_rn_evt_cap_mask_t event_capabilities = {0};

esp_avrc_rn_evt_bit_mask_operation(
    ESP_AVRC_BIT_MASK_OP_SET,
    &event_capabilities,
    ESP_AVRC_RN_VOLUME_CHANGE
);

result =
    esp_avrc_tg_set_rn_evt_cap(
        &event_capabilities
    );

if (result != ESP_OK) {
    ESP_LOGE(
        TAG,
        "AVRCP-Lautstaerke-Unterstuetzung konnte nicht aktiviert werden: %s",
        esp_err_to_name(result)
    );

    return result;
}

ESP_LOGI(
    TAG,
    "AVRCP absolute Lautstaerke aktiviert"
);
    result =
        esp_a2d_sink_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "A2DP Sink konnte nicht initialisiert werden: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    result =
        esp_bt_gap_set_device_name(
            "OpenAudio32"
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Bluetooth-Gerätename konnte nicht gesetzt werden: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    result =
        esp_bt_gap_set_scan_mode(
            ESP_BT_CONNECTABLE,
            ESP_BT_GENERAL_DISCOVERABLE
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Bluetooth-Sichtbarkeit konnte nicht aktiviert werden: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    ESP_LOGI(
        TAG,
        "A2DP Sink gestartet"
    );

    ESP_LOGI(
        TAG,
        "Bluetooth-Name: OpenAudio32"
    );

    ESP_LOGI(
        TAG,
        "Gerät ist sichtbar und verbindbar"
    );

    return ESP_OK;
}
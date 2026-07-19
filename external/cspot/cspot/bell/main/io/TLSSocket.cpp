#include "TLSSocket.h"

#include <cstring>
#include <stdexcept>
#include <string>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include "freertos/task.h"
#include "BellLogger.h"
#include "X509Bundle.h"

/**
 * Platform TLSSocket implementation for mbedTLS.
 */
bell::TLSSocket::TLSSocket()
{
    isClosed = true;

    mbedtls_net_init(&server_fd);
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_entropy_init(&entropy);

    static const char *personalization =
        "OpenAudio32-TLSSocket";

    const int result =
        mbedtls_ctr_drbg_seed(
            &ctr_drbg,
            mbedtls_entropy_func,
            &entropy,
            reinterpret_cast<const unsigned char *>(
                personalization
            ),
            strlen(personalization)
        );

    if (result != 0) {
        BELL_LOG(
            error,
            "http_tls",
            "mbedtls_ctr_drbg_seed fehlgeschlagen: -0x%04X",
            static_cast<unsigned int>(-result)
        );

        throw std::runtime_error(
            "mbedtls_ctr_drbg_seed failed"
        );
    }
}
bell::TLSSocket::~TLSSocket()

{

    close();

    mbedtls_ctr_drbg_free(

        &ctr_drbg

    );

    mbedtls_entropy_free(

        &entropy

    );

}
void bell::TLSSocket::open(
    const std::string &hostUrl,
    uint16_t port
)
{
    ESP_LOGI(
        "TLSSocket",
        "TLS-Verbindung zu %s:%u wird aufgebaut",
        hostUrl.c_str(),
        static_cast<unsigned int>(port)
    );

    if (hostUrl.empty()) {
        throw std::runtime_error(
            "TLS hostname is empty"
        );
    }

    /*
     * Falls das Objekt erneut verwendet wird, alte Zustände
     * vollständig zurücksetzen.
     */
    mbedtls_net_free(&server_fd);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);

    mbedtls_net_init(&server_fd);
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);

    isClosed = true;

    const std::string portString =
        std::to_string(port);

    int result =
        mbedtls_net_connect(
            &server_fd,
            hostUrl.c_str(),
            portString.c_str(),
            MBEDTLS_NET_PROTO_TCP
        );

    if (result != 0) {
        ESP_LOGE(
            "TLSSocket",
            "TCP-Verbindung zu %s:%s fehlgeschlagen: -0x%04X",
            hostUrl.c_str(),
            portString.c_str(),
            static_cast<unsigned int>(-result)
        );

        throw std::runtime_error(
            "mbedtls_net_connect failed"
        );
    }

    ESP_LOGI(
        "TLSSocket",
        "TCP-Verbindung hergestellt, Socket=%d",
        server_fd.fd
    );

    result =
        mbedtls_ssl_config_defaults(
            &conf,
            MBEDTLS_SSL_IS_CLIENT,
            MBEDTLS_SSL_TRANSPORT_STREAM,
            MBEDTLS_SSL_PRESET_DEFAULT
        );

    if (result != 0) {
        ESP_LOGE(
            "TLSSocket",
            "mbedtls_ssl_config_defaults fehlgeschlagen: -0x%04X",
            static_cast<unsigned int>(-result)
        );

        throw std::runtime_error(
            "mbedtls_ssl_config_defaults failed"
        );
    }

    ESP_LOGI(
        "TLSSocket",
        "TLS-Standardkonfiguration erstellt"
    );

    mbedtls_ssl_conf_rng(
        &conf,
        mbedtls_ctr_drbg_random,
        &ctr_drbg
    );

    if (bell::X509Bundle::shouldVerify()) {
        ESP_LOGI(
            "TLSSocket",
            "ESP-Zertifikatsbundle wird verwendet"
        );

        bell::X509Bundle::attach(
            &conf
        );

        mbedtls_ssl_conf_authmode(
            &conf,
            MBEDTLS_SSL_VERIFY_REQUIRED
        );
    } else {
        ESP_LOGW(
            "TLSSocket",
            "TLS-Zertifikatspruefung ist deaktiviert"
        );

        mbedtls_ssl_conf_authmode(
            &conf,
            MBEDTLS_SSL_VERIFY_NONE
        );
    }
    ESP_LOGI(

        "TLSSocket",

        "Heap vor ssl_setup: frei intern=%u, groesster intern=%u, frei PSRAM=%u",

        static_cast<unsigned>(

            heap_caps_get_free_size(

                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT

            )

        ),

        static_cast<unsigned>(

            heap_caps_get_largest_free_block(

                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT

            )

        ),

        static_cast<unsigned>(

            heap_caps_get_free_size(

                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT

            )

        )

    );
    result =
        mbedtls_ssl_setup(
            &ssl,
            &conf
        );

    if (result != 0) {
        ESP_LOGE(
            "TLSSocket",
            "mbedtls_ssl_setup fehlgeschlagen: -0x%04X",
            static_cast<unsigned int>(-result)
        );

        throw std::runtime_error(
            "mbedtls_ssl_setup failed"
        );
    }

    ESP_LOGI(
        "TLSSocket",
        "TLS-Kontext eingerichtet"
    );

    result =
        mbedtls_ssl_set_hostname(
            &ssl,
            hostUrl.c_str()
        );

    if (result != 0) {
        ESP_LOGE(
            "TLSSocket",
            "mbedtls_ssl_set_hostname fehlgeschlagen: -0x%04X",
            static_cast<unsigned int>(-result)
        );

        throw std::runtime_error(
            "mbedtls_ssl_set_hostname failed"
        );
    }

    mbedtls_ssl_set_bio(
        &ssl,
        &server_fd,
        mbedtls_net_send,
        mbedtls_net_recv,
        nullptr
    );

    ESP_LOGI(
        "TLSSocket",
        "Starte TLS-Handshake"
    );

    do {
        result =
            mbedtls_ssl_handshake(
                &ssl
            );

        if (result == MBEDTLS_ERR_SSL_WANT_READ ||
            result == MBEDTLS_ERR_SSL_WANT_WRITE) {

            vTaskDelay(
                pdMS_TO_TICKS(10)
            );

            continue;
        }

        if (result != 0) {
            ESP_LOGE(
                "TLSSocket",
                "TLS-Handshake fehlgeschlagen: -0x%04X",
                static_cast<unsigned int>(-result)
            );

            throw std::runtime_error(
                "mbedtls_ssl_handshake failed"
            );
        }

    } while (result != 0);

    isClosed = false;

    ESP_LOGI(
        "TLSSocket",
        "TLS-Verbindung zu %s:%u hergestellt",
        hostUrl.c_str(),
        static_cast<unsigned int>(port)
    );
}

size_t bell::TLSSocket::read(
    uint8_t *buffer,
    size_t length
)
{
    const int result =
        mbedtls_ssl_read(
            &ssl,
            buffer,
            length
        );

    if (result <= 0) {
        return 0;
    }

    return static_cast<size_t>(result);
}

size_t bell::TLSSocket::write(
    uint8_t *buffer,
    size_t length
)
{
    const int result =
        mbedtls_ssl_write(
            &ssl,
            buffer,
            length
        );

    if (result <= 0) {
        return 0;
    }

    return static_cast<size_t>(result);
}

size_t bell::TLSSocket::poll()
{
    return static_cast<size_t>(
        mbedtls_ssl_get_bytes_avail(&ssl)
    );
}

bool bell::TLSSocket::isOpen()
{
    return !isClosed;
}

void bell::TLSSocket::close()

{

    if (!isClosed) {

        mbedtls_ssl_close_notify(

            &ssl

        );

    }

    mbedtls_net_free(

        &server_fd

    );

    mbedtls_ssl_free(

        &ssl

    );

    mbedtls_ssl_config_free(

        &conf

    );

    isClosed = true;

}
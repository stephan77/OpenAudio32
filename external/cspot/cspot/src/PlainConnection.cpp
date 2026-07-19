#include "PlainConnection.h"

#ifndef _WIN32
#include <netdb.h>  // for addrinfo, freeaddrinfo, getaddrinfo
#include <netdb.h>
#include <netinet/in.h>   // for IPPROTO_IP, IPPROTO_TCP
#include <netinet/tcp.h>  // for TCP_NODELAY
#include <sys/errno.h>    // for EAGAIN, EINTR, ETIMEDOUT, errno
#include <sys/socket.h>   // for setsockopt, connect, recv, send, shutdown
#include <sys/time.h>     // for timeval
#include <cstring>        // for memset
#include <stdexcept>      // for runtime_error
#else
#include <ws2tcpip.h>
#endif
#include "BellLogger.h"  // for AbstractLogger
#include "Logger.h"      // for CSPOT_LOG
#include "Packet.h"      // for cspot
#include "Utils.h"       // for extract, pack
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include "freertos/task.h"
using namespace cspot;

static int getErrno() {
#ifdef _WIN32
  int code = WSAGetLastError();
  if (code == WSAETIMEDOUT)
    return ETIMEDOUT;
  if (code == WSAEINTR)
    return EINTR;
  return code;
#else
  return errno;
#endif
}

PlainConnection::PlainConnection() {
  this->apSock = -1;
};

PlainConnection::~PlainConnection() {
  this->close();
};

void PlainConnection::connect(const std::string& apAddress)
{
    ESP_LOGI(
        "PlainConnection",
        "Verbinde mit Spotify AP: %s",
        apAddress.c_str()
    );

    const size_t separatorPosition =
        apAddress.rfind(':');

    if (separatorPosition == std::string::npos ||
        separatorPosition == 0 ||
        separatorPosition + 1 >= apAddress.size()) {

        ESP_LOGE(
            "PlainConnection",
            "Ungueltige Spotify-AP-Adresse: %s",
            apAddress.c_str()
        );

        throw std::runtime_error(
            "Invalid Spotify AP address"
        );
    }

    const std::string hostname =
        apAddress.substr(
            0,
            separatorPosition
        );

    const std::string portString =
        apAddress.substr(
            separatorPosition + 1
        );

    ESP_LOGI(
        "PlainConnection",
        "Hostname=%s, Port=%s",
        hostname.c_str(),
        portString.c_str()
    );

    struct addrinfo hints;
    memset(
        &hints,
        0,
        sizeof(hints)
    );

    /*
     * Spotify liefert momentan IPv4-Namen. AF_UNSPEC ist trotzdem
     * robuster, weil sowohl IPv4 als auch IPv6 akzeptiert werden.
     */
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo *addressList = nullptr;

    const int resolverResult =
        getaddrinfo(
            hostname.c_str(),
            portString.c_str(),
            &hints,
            &addressList
        );

    if (resolverResult != 0 ||
        addressList == nullptr) {

        ESP_LOGE(
            "PlainConnection",
            "DNS-Aufloesung fuer %s fehlgeschlagen: %d",
            hostname.c_str(),
            resolverResult
        );

        throw std::runtime_error(
            "Spotify AP DNS resolution failed"
        );
    }

    ESP_LOGI(
        "PlainConnection",
        "DNS-Aufloesung erfolgreich"
    );

    int connectedSocket = -1;

    for (struct addrinfo *address = addressList;
         address != nullptr;
         address = address->ai_next) {

        ESP_LOGI(
            "PlainConnection",
            "Teste Adresse: Familie=%d, Typ=%d, Protokoll=%d",
            address->ai_family,
            address->ai_socktype,
            address->ai_protocol
        );

        const int candidateSocket =
            socket(
                address->ai_family,
                address->ai_socktype,
                address->ai_protocol
            );

        if (candidateSocket < 0) {
            ESP_LOGW(
                "PlainConnection",
                "Socket konnte nicht erstellt werden, errno=%d",
                errno
            );

            continue;
        }

        ESP_LOGI(
            "PlainConnection",
            "Socket erstellt: %d",
            candidateSocket
        );

        const int connectResult =
            ::connect(
                candidateSocket,
                address->ai_addr,
                address->ai_addrlen
            );

        if (connectResult == 0) {
            connectedSocket = candidateSocket;

            ESP_LOGI(
                "PlainConnection",
                "TCP-Verbindung zu %s:%s hergestellt, Socket=%d",
                hostname.c_str(),
                portString.c_str(),
                connectedSocket
            );

            break;
        }

        ESP_LOGW(
            "PlainConnection",
            "Verbindungsversuch fehlgeschlagen, errno=%d",
            errno
        );

#ifdef _WIN32
        closesocket(candidateSocket);
#else
        ::close(candidateSocket);
#endif
    }

    freeaddrinfo(addressList);
    addressList = nullptr;

    if (connectedSocket < 0) {
        ESP_LOGE(
            "PlainConnection",
            "Keine Verbindung zu Spotify AP %s:%s moeglich",
            hostname.c_str(),
            portString.c_str()
        );

        throw std::runtime_error(
            "Cannot connect to Spotify AP"
        );
    }

    this->apSock = connectedSocket;

#ifdef _WIN32
    uint32_t timeoutValue = 3000;
#else
    struct timeval timeoutValue;
    timeoutValue.tv_sec = 13;
    timeoutValue.tv_usec = 0;
#endif

    if (setsockopt(
            this->apSock,
            SOL_SOCKET,
            SO_RCVTIMEO,
            reinterpret_cast<const char *>(&timeoutValue),
            sizeof(timeoutValue)
        ) != 0) {

        ESP_LOGW(
            "PlainConnection",
            "SO_RCVTIMEO konnte nicht gesetzt werden, errno=%d",
            errno
        );
    }

    if (setsockopt(
            this->apSock,
            SOL_SOCKET,
            SO_SNDTIMEO,
            reinterpret_cast<const char *>(&timeoutValue),
            sizeof(timeoutValue)
        ) != 0) {

        ESP_LOGW(
            "PlainConnection",
            "SO_SNDTIMEO konnte nicht gesetzt werden, errno=%d",
            errno
        );
    }

    int noDelay = 1;

    if (setsockopt(
            this->apSock,
            IPPROTO_TCP,
            TCP_NODELAY,
            reinterpret_cast<const char *>(&noDelay),
            sizeof(noDelay)
        ) != 0) {

        ESP_LOGW(
            "PlainConnection",
            "TCP_NODELAY konnte nicht gesetzt werden, errno=%d",
            errno
        );
    }

    ESP_LOGI(
        "PlainConnection",
        "Spotify-AP-Socket vollstaendig konfiguriert"
    );
}

std::vector<uint8_t> PlainConnection::recvPacket() {
  // Read packet size
  std::vector<uint8_t> packetBuffer(4);
  readBlock(packetBuffer.data(), 4);
  uint32_t packetSize = ntohl(extract<uint32_t>(packetBuffer, 0));

  packetBuffer.resize(packetSize, 0);

  // Read actual data
  readBlock(packetBuffer.data() + 4, packetSize - 4);

  return packetBuffer;
}

std::vector<uint8_t> PlainConnection::sendPrefixPacket(
    const std::vector<uint8_t>& prefix, const std::vector<uint8_t>& data) {
  // Calculate full packet length
  uint32_t actualSize = prefix.size() + data.size() + sizeof(uint32_t);

  // Packet structure [PREFIX] + [SIZE] +  [DATA]
  auto sizeRaw = pack<uint32_t>(htonl(actualSize));
  sizeRaw.insert(sizeRaw.begin(), prefix.begin(), prefix.end());
  sizeRaw.insert(sizeRaw.end(), data.begin(), data.end());

  // Actually write it to the server
  writeBlock(sizeRaw);

  return sizeRaw;
}

void PlainConnection::readBlock(const uint8_t* dst, size_t size)
{
    size_t idx = 0;

    while (idx < size)
    {
        const ssize_t n = recv(
            this->apSock,
            reinterpret_cast<char*>(
                const_cast<uint8_t*>(&dst[idx])
            ),
            size - idx,
            0
        );

        if (n > 0)
        {
            idx += static_cast<size_t>(n);
            continue;
        }

        if (n == 0)
        {
            ESP_LOGE(
                "PlainConnection",
                "Spotify hat die TCP-Verbindung geschlossen"
            );

            throw std::runtime_error(
                "Spotify closed connection"
            );
        }

        const int error_code = getErrno();

        switch (error_code)
        {
            case EAGAIN:
            //case EWOULDBLOCK:
            case ETIMEDOUT:
            {
                /*
                 * Kein Fehler der Verbindung.
                 * Innerhalb des Empfangs-Timeouts kam nur kein Paket.
                 */
                if (timeoutHandler &&
                    timeoutHandler())
                {
                    ESP_LOGE(
                        "PlainConnection",
                        "Spotify-Verbindung wegen Session-Timeout beendet"
                    );

                    throw std::runtime_error(
                        "Reconnection required"
                    );
                }

                vTaskDelay(
                    pdMS_TO_TICKS(10)
                );

                continue;
            }

            case EINTR:
                continue;

            default:
                ESP_LOGE(
                    "PlainConnection",
                    "recv() fehlgeschlagen, errno=%d",
                    error_code
                );

                throw std::runtime_error(
                    "Error in read"
                );
        }
    }
}

size_t PlainConnection::writeBlock(const std::vector<uint8_t>& data) {
  unsigned int idx = 0;
  ssize_t n;

  int retries = 0;

  while (idx < data.size()) {
  WRITE:
    if ((n = send(this->apSock, (char*)&data[idx],
                  data.size() - idx < 64 ? data.size() - idx : 64, 0)) <= 0) {
      switch (getErrno()) {
        case EAGAIN:
        case ETIMEDOUT:
          if (timeoutHandler()) {
            throw std::runtime_error("Reconnection required");
          }
          goto WRITE;
        case EINTR:
          break;
        default:
          if (retries++ > 4)
            throw std::runtime_error("Error in write");
          goto WRITE;
      }
    }
    idx += n;
  }

  return data.size();
}

void PlainConnection::close() {
  if (this->apSock < 0)
    return;

  CSPOT_LOG(info, "Closing socket...");
  shutdown(this->apSock, SHUT_RDWR);
#ifdef _WIN32
  closesocket(this->apSock);
#else
  ::close(this->apSock);
#endif
  this->apSock = -1;
}

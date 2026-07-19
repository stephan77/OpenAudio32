#include "MercurySession.h"

#include <string.h>     // for memcpy
#include <memory>       // for shared_ptr
#include <mutex>        // for scoped_lock
#include <stdexcept>    // for runtime_error
#include <type_traits>  // for remove_extent_t, __underlying_type_impl<>:...
#include <utility>      // for pair
#ifndef _WIN32
#include <arpa/inet.h>  // for htons, ntohs, htonl, ntohl
#endif
#include "BellLogger.h"         // for AbstractLogger
#include "BellTask.h"           // for Task
#include "BellUtils.h"          // for BELL_SLEEP_MS
#include "Logger.h"             // for CSPOT_LOG
#include "NanoPBHelper.h"       // for pbPutString, pbDecode, pbEncode
#include "PlainConnection.h"    // for PlainConnection
#include "ShannonConnection.h"  // for ShannonConnection
#include "TimeProvider.h"       // for TimeProvider
#include "Utils.h"              // for extract, pack, hton64

using namespace cspot;

MercurySession::MercurySession(std::shared_ptr<TimeProvider> timeProvider)
    : bell::Task("mercury_dispatcher", 4 * 1024, 3, 1) {
  this->timeProvider = timeProvider;
}

MercurySession::~MercurySession() {
  std::scoped_lock lock(this->isRunningMutex);
}

void MercurySession::runTask() {
  isRunning = true;
  std::scoped_lock lock(this->isRunningMutex);

  this->executeEstabilishedCallback = true;
  while (isRunning) {
    cspot::Packet packet = {};
    try {
      packet = shanConn->recvPacket();
      CSPOT_LOG(info, "Received packet, command: %d", packet.command);

      if (static_cast<RequestType>(packet.command) == RequestType::PING) {
        timeProvider->syncWithPingPacket(packet.data);

        this->lastPingTimestamp = timeProvider->getSyncedTimestamp();
        this->shanConn->sendPacket(0x49, packet.data);
      } else {
        this->packetQueue.push(packet);
      }
    } catch (const std::runtime_error& e) {
      CSPOT_LOG(error, "Error while receiving packet: %s", e.what());
      failAllPending();

      if (!isRunning)
        return;

      reconnect();
      continue;
    }
  }
}

void MercurySession::reconnect() {
  isReconnecting = true;

  try {
    this->conn = nullptr;
    this->shanConn = nullptr;

    this->connectWithRandomAp();
    this->authenticate(this->authBlob);

    CSPOT_LOG(info, "Reconnection successful");

    BELL_SLEEP_MS(100);

    lastPingTimestamp = timeProvider->getSyncedTimestamp();
    isReconnecting = false;

    this->executeEstabilishedCallback = true;
  } catch (...) {
    CSPOT_LOG(error, "Cannot reconnect, will retry in 5s");
    BELL_SLEEP_MS(5000);

    if (isRunning) {
      return reconnect();
    }
  }
}

void MercurySession::setConnectedHandler(
    ConnectionEstabilishedCallback callback) {
  this->connectionReadyCallback = callback;
}

bool MercurySession::triggerTimeout() {
  if (!isRunning)
    return true;
  auto currentTimestamp = timeProvider->getSyncedTimestamp();

  if (currentTimestamp - this->lastPingTimestamp > PING_TIMEOUT_MS) {
    CSPOT_LOG(debug, "Reconnection required, no ping received");
    return true;
  }

  return false;
}

void MercurySession::unregister(uint64_t sequenceId) {
  auto callback = this->callbacks.find(sequenceId);

  if (callback != this->callbacks.end()) {
    this->callbacks.erase(callback);
  }
}

void MercurySession::unregisterAudioKey(uint32_t sequenceId) {
  auto callback = this->audioKeyCallbacks.find(sequenceId);

  if (callback != this->audioKeyCallbacks.end()) {
    this->audioKeyCallbacks.erase(callback);
  }
}

void MercurySession::disconnect() {
  CSPOT_LOG(info, "Disconnecting mercury session");
  this->isRunning = false;
  conn->close();
  std::scoped_lock lock(this->isRunningMutex);
}

std::string MercurySession::getCountryCode() {
  return this->countryCode;
}

void MercurySession::handlePacket() {
  Packet packet = {};

  this->packetQueue.wtpop(packet, 200);

  if (executeEstabilishedCallback && this->connectionReadyCallback != nullptr) {
    executeEstabilishedCallback = false;
    this->connectionReadyCallback();
  }

  switch (static_cast<RequestType>(packet.command)) {
    case RequestType::COUNTRY_CODE_RESPONSE: {
      this->countryCode = std::string();
      this->countryCode.resize(2);
      memcpy(this->countryCode.data(), packet.data.data(), 2);
      CSPOT_LOG(debug, "Received country code %s", this->countryCode.c_str());
      break;
    }
    case RequestType::AUDIO_KEY_FAILURE_RESPONSE:
    case RequestType::AUDIO_KEY_SUCCESS_RESPONSE: {
      // this->lastRequestTimestamp = -1;

      // First four bytes mark the sequence id
      auto seqId = ntohl(extract<uint32_t>(packet.data, 0));

      if (this->audioKeyCallbacks.count(seqId) > 0) {
        auto success = static_cast<RequestType>(packet.command) ==
                       RequestType::AUDIO_KEY_SUCCESS_RESPONSE;
        this->audioKeyCallbacks[seqId](success, packet.data);
      }

      break;
    }
    case RequestType::SEND:
    case RequestType::SUB:
    case RequestType::UNSUB: {
      CSPOT_LOG(debug, "Received mercury packet");

      auto response = this->decodeResponse(packet.data);
auto callback =
    this->callbacks.find(
        response.sequenceId
    );

if (callback != this->callbacks.end()) {
  auto function =
      callback->second;

  this->callbacks.erase(
      callback
  );

  if (function) {
    function(response);
  } else {
    CSPOT_LOG(
        error,
        "Mercury-Callback fuer Sequenz %llu ist leer",
        static_cast<unsigned long long>(
            response.sequenceId
        )
    );
  }
} else {
  CSPOT_LOG(
      info,
      "Kein Mercury-Callback fuer Sequenz %llu registriert",
      static_cast<unsigned long long>(
          response.sequenceId
      )
  );
}
      break;
    }
    case RequestType::SUBRES: {
  CSPOT_LOG(
      info,
      "Mercury SUBRES empfangen, Paketgroesse=%u",
      static_cast<unsigned int>(packet.data.size())
  );

  Response response = {};

  try {
    response = decodeResponse(packet.data);
  } catch (const std::exception& exception) {
    CSPOT_LOG(
        error,
        "Mercury SUBRES konnte nicht dekodiert werden: %s",
        exception.what()
    );
    break;
  } catch (...) {
    CSPOT_LOG(
        error,
        "Mercury SUBRES konnte nicht dekodiert werden: unbekannter Fehler"
    );
    break;
  }

const std::string uri(
    response.mercuryHeader.uri,
    strnlen(
        response.mercuryHeader.uri,
        sizeof(response.mercuryHeader.uri)
    )
);

  CSPOT_LOG(
      info,
      "Mercury SUBRES: URI=<%s>, Sequenz=%llu, Teile=%u, Subscriptions=%u",
      uri.c_str(),
      static_cast<unsigned long long>(response.sequenceId),
      static_cast<unsigned int>(response.parts.size()),
      static_cast<unsigned int>(this->subscriptions.size())
  );

  auto subscription =
      this->subscriptions.find(uri);

  if (subscription != this->subscriptions.end() &&
      subscription->second) {

    CSPOT_LOG(
        info,
        "Mercury SUBRES: Passender Callback fuer URI gefunden"
    );

    subscription->second(response);
    break;
  }

  CSPOT_LOG(
      info,
      "Mercury SUBRES: Kein exakter Callback fuer URI <%s>",
      uri.c_str()
  );

  break;
}
    default:
      break;
  }
}

void MercurySession::failAllPending() {
  Response response = {};
  response.fail = true;

  // Fail all callbacks
  for (auto& it : this->callbacks) {
    it.second(response);
  }

  // Fail all subscriptions
  for (auto& it : this->subscriptions) {
    it.second(response);
  }

  // Remove references
  this->subscriptions = {};
  this->callbacks = {};
}

MercurySession::Response MercurySession::decodeResponse(
    const std::vector<uint8_t>& data
) {
  Response response = {};
  response.parts = {};
  response.fail = true;

  /*
   * Minimaler Aufbau:
   * 2 Byte Sequence-Length
   * 8 Byte Sequence-ID
   * 1 Byte Flags
   * 2 Byte Parts-Anzahl
   * 2 Byte Header-Länge
   */
  if (data.size() < 15) {
    throw std::runtime_error(
        "Mercury response is shorter than 15 bytes"
    );
  }

  const uint16_t sequenceLength =
      ntohs(
          extract<uint16_t>(
              data,
              0
          )
      );

  if (sequenceLength != 8) {
    CSPOT_LOG(
        info,
        "Mercury: Unerwartete Sequenzlaenge=%u",
        static_cast<unsigned int>(sequenceLength)
    );
  }

  response.sequenceId =
      hton64(
          extract<uint64_t>(
              data,
              2
          )
      );

  response.flags =
      data[10];

  const uint16_t partsNumber =
      ntohs(
          extract<uint16_t>(
              data,
              11
          )
      );

  const uint16_t headerSize =
      ntohs(
          extract<uint16_t>(
              data,
              13
          )
      );

  if (15U + static_cast<size_t>(headerSize) >
      data.size()) {

    throw std::runtime_error(
        "Mercury header exceeds packet size"
    );
  }

  std::vector<uint8_t> headerBytes(
      data.begin() + 15,
      data.begin() + 15 + headerSize
  );

  size_t position =
      15U + static_cast<size_t>(headerSize);

  while (position < data.size()) {
    if (position + 2U > data.size()) {
      throw std::runtime_error(
          "Mercury part length is truncated"
      );
    }

    const uint16_t partSize =
        ntohs(
            extract<uint16_t>(
                data,
                position
            )
        );

    position += 2U;

    if (position + static_cast<size_t>(partSize) >
        data.size()) {

      throw std::runtime_error(
          "Mercury part exceeds packet size"
      );
    }

    response.parts.emplace_back(
        data.begin() + position,
        data.begin() + position + partSize
    );

    position +=
        static_cast<size_t>(partSize);
  }

  pbDecode(
      response.mercuryHeader,
      Header_fields,
      headerBytes
  );

  CSPOT_LOG(
      info,
      "Mercury dekodiert: Sequenz=%llu, Flags=0x%02X, angekuendigte Teile=%u, gelesene Teile=%u",
      static_cast<unsigned long long>(
          response.sequenceId
      ),
      static_cast<unsigned int>(
          response.flags
      ),
      static_cast<unsigned int>(
          partsNumber
      ),
      static_cast<unsigned int>(
          response.parts.size()
      )
  );

  response.fail = false;
  return response;
}

uint64_t MercurySession::executeSubscription(RequestType method,
                                             const std::string& uri,
                                             ResponseCallback callback,
                                             ResponseCallback subscription,
                                             DataParts& payload) {
  CSPOT_LOG(debug, "Executing Mercury Request, type %s",
            RequestTypeMap[method].c_str());

  // Encode header
  pbPutString(uri, tempMercuryHeader.uri);
  pbPutString(RequestTypeMap[method], tempMercuryHeader.method);

  tempMercuryHeader.has_method = true;
  tempMercuryHeader.has_uri = true;

  // GET and SEND are actually the same. Therefore the override
  // The difference between them is only in header's method
  if (method == RequestType::GET) {
    method = RequestType::SEND;
  }

  if (method == RequestType::SUB) {
    this->subscriptions.insert({uri, subscription});
  }

  auto headerBytes = pbEncode(Header_fields, &tempMercuryHeader);

  this->callbacks.insert({sequenceId, callback});

  // Structure: [Sequence size] [SequenceId] [0x1] [Payloads number]
  // [Header size] [Header] [Payloads (size + data)]

  // Pack sequenceId
  auto sequenceIdBytes = pack<uint64_t>(hton64(this->sequenceId));
  auto sequenceSizeBytes = pack<uint16_t>(htons(sequenceIdBytes.size()));

  sequenceIdBytes.insert(sequenceIdBytes.begin(), sequenceSizeBytes.begin(),
                         sequenceSizeBytes.end());
  sequenceIdBytes.push_back(0x01);

  auto payloadNum = pack<uint16_t>(htons(payload.size() + 1));
  sequenceIdBytes.insert(sequenceIdBytes.end(), payloadNum.begin(),
                         payloadNum.end());

  auto headerSizePayload = pack<uint16_t>(htons(headerBytes.size()));
  sequenceIdBytes.insert(sequenceIdBytes.end(), headerSizePayload.begin(),
                         headerSizePayload.end());
  sequenceIdBytes.insert(sequenceIdBytes.end(), headerBytes.begin(),
                         headerBytes.end());

  // Encode all the payload parts
  for (int x = 0; x < payload.size(); x++) {
    headerSizePayload = pack<uint16_t>(htons(payload[x].size()));
    sequenceIdBytes.insert(sequenceIdBytes.end(), headerSizePayload.begin(),
                           headerSizePayload.end());
    sequenceIdBytes.insert(sequenceIdBytes.end(), payload[x].begin(),
                           payload[x].end());
  }

  // Bump sequence id
  this->sequenceId += 1;

  try {
    this->shanConn->sendPacket(
        static_cast<std::underlying_type<RequestType>::type>(method),
        sequenceIdBytes);
  } catch (...) {
    // @TODO: handle disconnect
  }

  return this->sequenceId - 1;
}

uint32_t MercurySession::requestAudioKey(const std::vector<uint8_t>& trackId,
                                         const std::vector<uint8_t>& fileId,
                                         AudioKeyCallback audioCallback) {
  auto buffer = fileId;

  // Store callback
  this->audioKeyCallbacks.insert({this->audioKeySequence, audioCallback});

  // Structure: [FILEID] [TRACKID] [4 BYTES SEQUENCE ID] [0x00, 0x00]
  buffer.insert(buffer.end(), trackId.begin(), trackId.end());
  auto audioKeySequenceBuffer = pack<uint32_t>(htonl(this->audioKeySequence));
  buffer.insert(buffer.end(), audioKeySequenceBuffer.begin(),
                audioKeySequenceBuffer.end());
  auto suffix = std::vector<uint8_t>({0x00, 0x00});
  buffer.insert(buffer.end(), suffix.begin(), suffix.end());

  // Bump audio key sequence
  this->audioKeySequence += 1;

  // Used for broken connection detection
  // this->lastRequestTimestamp = timeProvider->getSyncedTimestamp();
  try {
    this->shanConn->sendPacket(
        static_cast<uint8_t>(RequestType::AUDIO_KEY_REQUEST_COMMAND), buffer);
  } catch (...) {
    // @TODO: Handle disconnect
  }
  return audioKeySequence - 1;
}

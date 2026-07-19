#include "SpircHandler.h"

#include <cstdint>      // for uint8_t
#include <memory>       // for shared_ptr, make_unique, unique_ptr
#include <type_traits>  // for remove_extent_t
#include <utility>      // for move

#include "BellLogger.h"      // for AbstractLogger
#include "CSpotContext.h"    // for Context::ConfigState, Context (ptr only)
#include "Logger.h"          // for CSPOT_LOG
#include "MercurySession.h"  // for MercurySession, MercurySession::Response
#include "NanoPBHelper.h"    // for pbDecode
#include "Packet.h"          // for cspot
#include "PlaybackState.h"   // for PlaybackState, PlaybackState::State
#include "TrackPlayer.h"     // for TrackPlayer
#include "TrackQueue.h"
#include "TrackReference.h"     // for TrackReference
#include "Utils.h"              // for stringHexToBytes
#include "pb_decode.h"          // for pb_release
#include "protobuf/spirc.pb.h"  // for Frame, State, Frame_fields, MessageTy...

using namespace cspot;

SpircHandler::SpircHandler(std::shared_ptr<cspot::Context> ctx) {
  this->playbackState = std::make_shared<PlaybackState>(ctx);
  this->trackQueue = std::make_shared<cspot::TrackQueue>(ctx, playbackState);

  auto EOFCallback = [this]() {
    if (trackQueue->isFinished()) {
      sendEvent(EventType::DEPLETED);
    }
  };

  auto trackLoadedCallback = [this](std::shared_ptr<QueuedTrack> track,
                                    bool paused = false) {
    playbackState->setPlaybackState(paused ? PlaybackState::State::Paused
                                           : PlaybackState::State::Playing);
    playbackState->updatePositionMs(track->requestedPosition);

    this->notify();

    // Send playback start event, pause/unpause per request
    sendEvent(EventType::PLAYBACK_START, (int)track->requestedPosition);
    sendEvent(EventType::PLAY_PAUSE, paused);
  };

  this->ctx = ctx;
  this->trackPlayer = std::make_shared<TrackPlayer>(
      ctx, trackQueue, EOFCallback, trackLoadedCallback);

  // Subscribe to mercury on session ready
  ctx->session->setConnectedHandler([this]() { this->subscribeToMercury(); });
}

void SpircHandler::subscribeToMercury() {
  const std::string subscriptionUri =
      "hm://remote/user/" +
      ctx->config.username +
      "/";

  CSPOT_LOG(
      info,
      "SPIRC: Starte Subscription fuer Benutzer <%s>",
      ctx->config.username.c_str()
  );

  CSPOT_LOG(
      info,
      "SPIRC: Subscription-URI <%s>",
      subscriptionUri.c_str()
  );

  auto responseLambda =
      [this](MercurySession::Response& res) {
        CSPOT_LOG(
            info,
            "SPIRC: Subscription-Response empfangen, fail=%d, Teile=%u",
            static_cast<int>(res.fail),
            static_cast<unsigned int>(res.parts.size())
        );

        if (res.fail) {
          CSPOT_LOG(
              error,
              "SPIRC: Subscription-Anfrage fehlgeschlagen"
          );
          return;
        }

        CSPOT_LOG(
            info,
            "SPIRC: Sende MessageTypeHello"
        );

        sendCmd(
            MessageType_kMessageTypeHello
        );

        CSPOT_LOG(
            info,
            "SPIRC: MessageTypeHello gesendet"
        );

        this->ctx->config.countryCode =
            this->ctx
                ->session
                ->getCountryCode();

        CSPOT_LOG(
            info,
            "SPIRC: Laendercode <%s>",
            this->ctx->config.countryCode.c_str()
        );
      };

  auto subscriptionLambda =
      [this](MercurySession::Response& res) {
        CSPOT_LOG(
            info,
            "SPIRC: Subscription-Daten empfangen, fail=%d, Teile=%u",
            static_cast<int>(res.fail),
            static_cast<unsigned int>(res.parts.size())
        );

        if (res.fail) {
          CSPOT_LOG(
              error,
              "SPIRC: Subscription-Daten enthalten Fehler"
          );
          return;
        }

        if (res.parts.empty()) {
          CSPOT_LOG(
              error,
              "SPIRC: Subscription-Daten enthalten keine Teile"
          );
          return;
        }

        CSPOT_LOG(
            info,
            "SPIRC: Verarbeite Frame mit %u Bytes",
            static_cast<unsigned int>(
                res.parts[0].size()
            )
        );

        this->handleFrame(
            res.parts[0]
        );
      };

  ctx->session->executeSubscription(
      MercurySession::RequestType::SUB,
      subscriptionUri,
      responseLambda,
      subscriptionLambda
  );

  CSPOT_LOG(
      info,
      "SPIRC: executeSubscription wurde aufgerufen"
  );
}

void SpircHandler::loadTrackFromURI(const std::string& uri) {}

void SpircHandler::notifyAudioEnded() {
  playbackState->updatePositionMs(0);
  notify();
  trackPlayer->resetState(true);
}

void SpircHandler::notifyAudioReachedPlayback() {
  int offset = 0;

  // get HEAD track
  auto currentTrack = trackQueue->consumeTrack(nullptr, offset);

  // Do not execute when meta is already updated
  if (trackQueue->notifyPending) {
    trackQueue->notifyPending = false;

    playbackState->updatePositionMs(currentTrack->requestedPosition);

    // Reset position in queued track
    currentTrack->requestedPosition = 0;
  } else {
    trackQueue->skipTrack(TrackQueue::SkipDirection::NEXT, false);
    playbackState->updatePositionMs(0);

    // we moved to next track, re-acquire currentTrack again
    currentTrack = trackQueue->consumeTrack(nullptr, offset);
  }

  this->notify();

  sendEvent(EventType::TRACK_INFO, currentTrack->trackInfo);
}

void SpircHandler::updatePositionMs(uint32_t position) {
  playbackState->updatePositionMs(position);
  notify();
}

void SpircHandler::disconnect() {
  this->trackQueue->stopTask();
  this->trackPlayer->stop();
  this->ctx->session->disconnect();
}

void SpircHandler::handleFrame(std::vector<uint8_t>& data) {
  if (data.empty()) {
    CSPOT_LOG(
        error,
        "SPIRC: Leeres Frame kann nicht dekodiert werden"
    );
    return;
  }

  CSPOT_LOG(
      info,
      "SPIRC: Dekodiere Frame, Groesse=%u Bytes",
      static_cast<unsigned>(
          data.size()
      )
  );

  playbackState->decodeRemoteFrame(data);

  CSPOT_LOG(
      info,
      "SPIRC: Nachrichtentyp=%d, Tracks=%u, Position=%u",
      static_cast<int>(
          playbackState->remoteFrame.typ
      ),
      static_cast<unsigned>(
          playbackState->remoteTracks.size()
      ),
      static_cast<unsigned>(
          playbackState->remoteFrame.position
      )
  );

  switch (playbackState->remoteFrame.typ) {
    case MessageType_kMessageTypeNotify: {
      CSPOT_LOG(debug, "Notify frame");

      // Pause the playback if another player took control
      if (playbackState->isActive() &&
          playbackState->remoteFrame.device_state.is_active) {
        CSPOT_LOG(debug, "Another player took control, pausing playback");
        playbackState->setActive(false);

        this->trackPlayer->stop();
        sendEvent(EventType::DISC);
      }
      break;
    }
    case MessageType_kMessageTypeSeek: {
      this->trackPlayer->seekMs(playbackState->remoteFrame.position);

      playbackState->updatePositionMs(playbackState->remoteFrame.position);

      notify();

      sendEvent(EventType::SEEK, (int)playbackState->remoteFrame.position);
      break;
    }
    case MessageType_kMessageTypeVolume:
      playbackState->setVolume(playbackState->remoteFrame.volume);
      this->notify();
      sendEvent(EventType::VOLUME, (int)playbackState->remoteFrame.volume);
      break;
    case MessageType_kMessageTypePause:
      setPause(true);
      break;
    case MessageType_kMessageTypePlay:
      setPause(false);
      break;
    case MessageType_kMessageTypeNext:
      if (nextSong()) {
        sendEvent(EventType::NEXT);
      }
      break;
    case MessageType_kMessageTypePrev:
      if (previousSong()) {
        sendEvent(EventType::PREV);
      }
      break;
    case MessageType_kMessageTypeLoad: {
      this->trackPlayer->start();

      CSPOT_LOG(debug, "Load frame %d!", playbackState->remoteTracks.size());

      if (playbackState->remoteTracks.size() == 0) {
        CSPOT_LOG(info, "No tracks in frame, stopping playback");
        break;
      }

      playbackState->setActive(true);

      playbackState->updatePositionMs(playbackState->remoteFrame.position);
      playbackState->setPlaybackState(PlaybackState::State::Playing);

      playbackState->syncWithRemote();

      // Update track list in case we have a new one
      trackQueue->updateTracks(playbackState->remoteFrame.state.position_ms,
                               true);

      this->notify();

      // Stop the current track, if any
      trackPlayer->resetState();
      break;
    }
    case MessageType_kMessageTypeReplace: {
      CSPOT_LOG(debug, "Got replace frame %d",
                playbackState->remoteTracks.size());
      playbackState->syncWithRemote();

      // 1st track is the current one, but update the position
      bool cleared = trackQueue->updateTracks(
          playbackState->remoteFrame.state.position_ms +
              ctx->timeProvider->getSyncedTimestamp() -
              playbackState->innerFrame.state.position_measured_at,
          false);

      this->notify();

      // need to re-load all if streaming track is completed
      if (cleared) {
        sendEvent(EventType::FLUSH);
        trackPlayer->resetState();
      }
      break;
    }
    case MessageType_kMessageTypeShuffle: {
      CSPOT_LOG(debug, "Got shuffle frame");
      this->notify();
      break;
    }
    case MessageType_kMessageTypeRepeat: {
      CSPOT_LOG(debug, "Got repeat frame");
      this->notify();
      break;
    }
    default:
      break;
  }
}

void SpircHandler::setRemoteVolume(int volume) {
  playbackState->setVolume(volume);
  notify();
}

void SpircHandler::notify() {
  this->sendCmd(MessageType_kMessageTypeNotify);
}

bool SpircHandler::skipSong(TrackQueue::SkipDirection dir) {
  bool skipped = trackQueue->skipTrack(dir);

  // Reset track state
  trackPlayer->resetState(!skipped);

  // send NEXT or PREV event only when successful
  return skipped;
}

bool SpircHandler::nextSong() {
  return skipSong(TrackQueue::SkipDirection::NEXT);
}

bool SpircHandler::previousSong() {
  return skipSong(TrackQueue::SkipDirection::PREV);
}

std::shared_ptr<TrackPlayer> SpircHandler::getTrackPlayer() {
  return this->trackPlayer;
}

void SpircHandler::sendCmd(MessageType typ) {
  if (!ctx ||
      !ctx->session ||
      !playbackState) {

    CSPOT_LOG(
        error,
        "SPIRC: sendCmd ohne gueltigen Kontext"
    );
    return;
  }

  const std::string commandUri =
      "hm://remote/user/" +
      ctx->config.username +
      "/";

  auto encodedFrame =
      playbackState
          ->encodeCurrentFrame(
              typ
          );

  CSPOT_LOG(
      info,
      "SPIRC: Sende Nachrichtentyp=%d, Groesse=%u Bytes",
      static_cast<int>(typ),
      static_cast<unsigned>(
          encodedFrame.size()
      )
  );

  auto responseLambda =
      [typ](MercurySession::Response& res) {
        CSPOT_LOG(
            info,
            "SPIRC: Antwort auf Nachrichtentyp=%d, fail=%d, parts=%u",
            static_cast<int>(typ),
            static_cast<int>(res.fail),
            static_cast<unsigned>(
                res.parts.size()
            )
        );
      };

  auto parts =
      MercurySession::DataParts{
          encodedFrame
      };

  ctx->session->execute(
      MercurySession::RequestType::SEND,
      commandUri,
      responseLambda,
      parts
  );
}
void SpircHandler::setEventHandler(EventHandler handler) {
  this->eventHandler = handler;
}

void SpircHandler::setPause(bool isPaused) {
  if (isPaused) {
    CSPOT_LOG(debug, "External pause command");
    playbackState->setPlaybackState(PlaybackState::State::Paused);
  } else {
    CSPOT_LOG(debug, "External play command");

    playbackState->setPlaybackState(PlaybackState::State::Playing);
  }
  notify();
  sendEvent(EventType::PLAY_PAUSE, isPaused);
}

void SpircHandler::sendEvent(EventType type) {
  if (!eventHandler) {
    CSPOT_LOG(
        info,
        "SPIRC-Event %d verworfen: kein EventHandler registriert",
        static_cast<int>(type)
    );
    return;
  }

  auto event =
      std::make_unique<Event>();

  event->eventType =
      type;

  event->data =
      false;

  eventHandler(
      std::move(event)
  );
}

void SpircHandler::sendEvent(
    EventType type,
    EventData data
) {
  if (!eventHandler) {
    CSPOT_LOG(
        info,
        "SPIRC-Event %d verworfen: kein EventHandler registriert",
        static_cast<int>(type)
    );
    return;
  }

  auto event =
      std::make_unique<Event>();

  event->eventType =
      type;

  event->data =
      std::move(data);

  eventHandler(
      std::move(event)
  );
}

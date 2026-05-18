#pragma once

#include <cstdint>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <eq2/db/repositories.h>
#include <eq2/net/socket_transport.h>
#include <eq2/protocol/play_character.h>
#include <eq2/protocol/login_world.h>
#include <eq2/protocol/stream_pipeline.h>
#include <eq2/world/session.h>

namespace eq2::world {

enum class LiveWorldEventType {
  client_accepted,
  session_requested,
  app_packet,
  malformed,
  disconnected,
  registered_with_login,
  registration_failed,
};

struct LiveWorldEvent {
  LiveWorldEventType type = LiveWorldEventType::malformed;
  eq2::net::SessionId session;
  std::uint16_t protocol_opcode = 0;
  std::uint16_t application_opcode = 0;
  std::string reason;
};

struct PendingWorldAccess {
  std::int32_t account_id = 0;
  std::int32_t character_id = 0;
  std::int32_t world_id = 0;
  std::int32_t access_key = 0;
};

class LiveWorldService {
 public:
  LiveWorldService(WorldServerConfig config,
                   eq2::db::CharacterListRepository& characters,
                   ZoneHandoff& zone_handoff)
      : config_(std::move(config)),
        characters_(characters),
        zone_handoff_(zone_handoff),
        core_(config_, characters_, zone_handoff_),
        client_transport_([this](eq2::net::SessionEvent event) {
          handle_client_event(std::move(event));
        }) {}

  LiveWorldService(const LiveWorldService&) = delete;
  auto operator=(const LiveWorldService&) -> LiveWorldService& = delete;

  auto start() -> bool {
    return client_transport_.start(eq2::net::SocketEndpoint{
        .address = config_.address,
        .port = config_.port,
    });
  }

  void stop() {
    login_client_.close();
    client_transport_.stop();
  }

  [[nodiscard]] auto port() const -> std::uint16_t {
    return client_transport_.port();
  }

  auto register_with_login() -> bool {
    auto frame = make_world_registration_frame(config_);
    if (!frame.has_value()) {
      record_event(LiveWorldEvent{
          .type = LiveWorldEventType::registration_failed,
          .reason = "failed to encode LSInfo frame",
      });
      return false;
    }

    const auto connected = login_client_.connect_to(eq2::net::SocketEndpoint{
        .address = config_.login_address,
        .port = config_.login_port,
    });
    const auto sent = connected && login_client_.send(*frame);
    if (!sent) {
      login_client_.close();
    }
    record_event(LiveWorldEvent{
        .type = sent ? LiveWorldEventType::registered_with_login
                     : LiveWorldEventType::registration_failed,
        .reason = sent ? "LSInfo sent" : "failed to connect to login",
    });
    return sent;
  }

  auto admit_login_handoff(const eq2::protocol::UserToWorldRequest& request)
      -> eq2::protocol::UserToWorldResponse {
    std::lock_guard lock(mutex_);
    const auto access_key = next_access_key_++;
    pending_access_.emplace(access_key, PendingWorldAccess{
                                            .account_id = request.login_account_id,
                                            .character_id = request.character_id,
                                            .world_id = request.world_id,
                                            .access_key = access_key,
                                        });

    return eq2::protocol::UserToWorldResponse{
        .login_account_id = request.login_account_id,
        .character_id = request.character_id,
        .world_id = request.world_id,
        .access_key = access_key,
        .response = 1,
        .ip_address = config_.address,
        .port = config_.port,
        .from_id = request.to_id,
        .to_id = request.from_id,
    };
  }

  [[nodiscard]] auto make_login_handoff_response_frame(
      const eq2::protocol::UserToWorldResponse& response) const
      -> std::optional<std::vector<std::uint8_t>> {
    const auto payload = eq2::protocol::encode_user_to_world_response_payload(response);
    return eq2::protocol::encode_interserver_packet(
        eq2::protocol::kServerOpUserToWorldResponse, payload);
  }

  auto open_session_from_handoff(eq2::net::SessionId session,
                                 std::int32_t account_id,
                                 std::int32_t access_key) -> std::optional<WorldClientSession> {
    std::lock_guard lock(mutex_);
    const auto iter = pending_access_.find(access_key);
    if (iter == pending_access_.end() || iter->second.account_id != account_id) {
      return std::nullopt;
    }

    pending_access_.erase(iter);
    return core_.open_session(session, account_id, access_key);
  }

  auto character_list(const WorldClientSession& session) -> std::vector<CharacterSummary> {
    return core_.character_list(session);
  }

  auto select_character(const WorldClientSession& session, std::int32_t character_id)
      -> CharacterSelectResult {
    return core_.select_character(session, character_id);
  }

  auto handoff_to_zone(const WorldClientSession& session, const CharacterSummary& character)
      -> ZoneHandoffResult {
    return core_.handoff_to_zone(session, character);
  }

  [[nodiscard]] auto make_play_character_response_payload(
      const WorldClientSession& session,
      const CharacterSelectResult& selection,
      std::uint16_t client_version) const -> std::vector<std::uint8_t> {
    return eq2::protocol::encode_play_character_response_payload(
        eq2::protocol::PlayCharacterResponse{
            .response = selection.accepted ? std::uint8_t{1} : std::uint8_t{0},
            .server = selection.accepted ? config_.address : std::string{},
            .port = selection.accepted ? config_.port : std::uint16_t{0},
            .account_id = selection.accepted ? session.account_id : 0,
            .access_code = selection.accepted ? session.access_key : 0,
        },
        client_version);
  }

  [[nodiscard]] auto events() const -> std::vector<LiveWorldEvent> {
    std::lock_guard lock(mutex_);
    return events_;
  }

 private:
  void handle_client_event(eq2::net::SessionEvent event) {
    std::lock_guard lock(mutex_);

    if (event.type == eq2::net::SessionEventType::accepted) {
      events_.push_back(LiveWorldEvent{
          .type = LiveWorldEventType::client_accepted,
          .session = event.session,
      });
      return;
    }

    if (event.type == eq2::net::SessionEventType::disconnected) {
      pipelines_.erase(event.session.value);
      events_.push_back(LiveWorldEvent{
          .type = LiveWorldEventType::disconnected,
          .session = event.session,
          .reason = event.reason,
      });
      return;
    }

    if (event.type != eq2::net::SessionEventType::received) {
      return;
    }

    eq2::protocol::StreamPipelineOptions pipeline_options;
    pipeline_options.application_opcode_width = config_.client_opcode_width;
    auto pipeline_result = pipelines_.try_emplace(event.session.value, pipeline_options);
    auto& pipeline = pipeline_result.first->second;
    auto result = pipeline.receive_datagram(event.bytes);
    for (const auto& packet : result.outbound) {
      client_transport_.send(event.session, packet);
    }
    for (const auto& stream_event : result.events) {
      record_stream_event(event.session, stream_event);
    }
  }

  void record_stream_event(eq2::net::SessionId session,
                           const eq2::protocol::StreamEvent& stream_event) {
    switch (stream_event.type) {
      case eq2::protocol::StreamEventType::session_requested:
        events_.push_back(LiveWorldEvent{
            .type = LiveWorldEventType::session_requested,
            .session = session,
            .protocol_opcode = stream_event.protocol_opcode,
        });
        break;
      case eq2::protocol::StreamEventType::app_packet:
        events_.push_back(LiveWorldEvent{
            .type = LiveWorldEventType::app_packet,
            .session = session,
            .protocol_opcode = stream_event.protocol_opcode,
            .application_opcode = stream_event.app_packet.has_value()
                                      ? stream_event.app_packet->opcode
                                      : std::uint16_t{0},
        });
        break;
      case eq2::protocol::StreamEventType::disconnected:
        pipelines_.erase(session.value);
        events_.push_back(LiveWorldEvent{
            .type = LiveWorldEventType::disconnected,
            .session = session,
            .protocol_opcode = stream_event.protocol_opcode,
        });
        break;
      case eq2::protocol::StreamEventType::malformed:
      case eq2::protocol::StreamEventType::unsupported:
      case eq2::protocol::StreamEventType::keep_alive:
        events_.push_back(LiveWorldEvent{
            .type = stream_event.type == eq2::protocol::StreamEventType::malformed
                        ? LiveWorldEventType::malformed
                        : LiveWorldEventType::app_packet,
            .session = session,
            .protocol_opcode = stream_event.protocol_opcode,
            .reason = "world client protocol event",
        });
        break;
    }
  }

  void record_event(LiveWorldEvent event) {
    std::lock_guard lock(mutex_);
    events_.push_back(std::move(event));
  }

  WorldServerConfig config_;
  eq2::db::CharacterListRepository& characters_;
  ZoneHandoff& zone_handoff_;
  WorldServer core_;
  eq2::net::TcpSocketServer client_transport_;
  eq2::net::TcpSocketClient login_client_;

  mutable std::mutex mutex_;
  std::unordered_map<std::uint64_t, eq2::protocol::StreamPipeline> pipelines_;
  std::unordered_map<std::int32_t, PendingWorldAccess> pending_access_;
  std::int32_t next_access_key_ = 0x500000;
  std::vector<LiveWorldEvent> events_;
};

}  // namespace eq2::world

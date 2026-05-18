#pragma once

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <eq2/core/endian.h>
#include <eq2/db/repositories.h>
#include <eq2/login/authentication.h>
#include <eq2/login/character_select.h>
#include <eq2/login/server.h>
#include <eq2/login/world_registration.h>
#include <eq2/protocol/application_packet.h>
#include <eq2/protocol/client_log.h>
#include <eq2/protocol/create_character.h>
#include <eq2/protocol/delete_character.h>
#include <eq2/protocol/interserver_packet.h>
#include <eq2/protocol/login_response.h>
#include <eq2/protocol/login_request.h>
#include <eq2/protocol/packet_buffer.h>
#include <eq2/protocol/play_character.h>
#include <eq2/protocol/protocol_packet.h>
#include <eq2/protocol/stream_pipeline.h>
#include <eq2/net/socket_transport.h>

namespace eq2::login {

inline constexpr std::uint16_t kLoginRequestAppOpcode = 0x1234;
inline constexpr std::uint16_t kLoginReplyAppOpcode = 0x1235;
inline constexpr std::uint16_t kWorldListReplyAppOpcode = 0x1236;
inline constexpr std::uint16_t kAllWorldsRequestAppOpcode = 0x1237;
inline constexpr std::uint16_t kCharactersRequestAppOpcode = 0x1238;
inline constexpr std::uint16_t kCharactersReplyAppOpcode = 0x1239;
inline constexpr std::uint16_t kCreateCharacterRequestAppOpcode = 0x123a;
inline constexpr std::uint16_t kCreateCharacterReplyAppOpcode = 0x123b;
inline constexpr std::uint16_t kDeleteCharacterRequestAppOpcode = 0x123c;
inline constexpr std::uint16_t kDeleteCharacterReplyAppOpcode = 0x123d;
inline constexpr std::uint16_t kPlayCharacterRequestAppOpcode = 0x123e;
inline constexpr std::uint16_t kPlayCharacterReplyAppOpcode = 0x123f;
inline constexpr std::uint16_t kClientCrashlogReplyAppOpcode = 0x1240;
inline constexpr std::uint16_t kClientEq2CrashlogReplyAppOpcode = 0x1241;
inline constexpr std::uint16_t kClientAlertlogReplyAppOpcode = 0x1242;
inline constexpr std::uint16_t kClientBaselogReplyAppOpcode = 0x1243;
inline constexpr std::uint16_t kClientVerifylogReplyAppOpcode = 0x1244;
inline constexpr std::uint16_t kKeyRequestAppOpcode = 0x0002;

enum class LiveLoginEventType {
  transport_accepted,
  session_requested,
  login_accepted,
  login_rejected,
  world_registered,
  world_status_updated,
  world_rejected,
  character_create_forwarded,
  character_create_accepted,
  character_create_rejected,
  character_updated,
  character_deleted,
  character_rejected,
  play_forwarded,
  play_accepted,
  play_rejected,
  client_log_recorded,
  client_log_rejected,
  key_requested,
  malformed,
  unsupported,
  disconnected,
};

struct LiveLoginEvent {
  LiveLoginEventType type = LiveLoginEventType::malformed;
  eq2::net::SessionId session;
  std::uint16_t protocol_opcode = 0;
  std::uint16_t application_opcode = 0;
  LoginReplyCode reply_code = LoginReplyCode::invalid_username_or_password;
  std::int16_t client_version = 0;
  std::string reason;
};

inline auto live_login_event_type_name(LiveLoginEventType type) -> std::string_view {
  switch (type) {
    case LiveLoginEventType::transport_accepted:
      return "transport_accepted";
    case LiveLoginEventType::session_requested:
      return "session_requested";
    case LiveLoginEventType::login_accepted:
      return "login_accepted";
    case LiveLoginEventType::login_rejected:
      return "login_rejected";
    case LiveLoginEventType::world_registered:
      return "world_registered";
    case LiveLoginEventType::world_status_updated:
      return "world_status_updated";
    case LiveLoginEventType::world_rejected:
      return "world_rejected";
    case LiveLoginEventType::character_create_forwarded:
      return "character_create_forwarded";
    case LiveLoginEventType::character_create_accepted:
      return "character_create_accepted";
    case LiveLoginEventType::character_create_rejected:
      return "character_create_rejected";
    case LiveLoginEventType::character_updated:
      return "character_updated";
    case LiveLoginEventType::character_deleted:
      return "character_deleted";
    case LiveLoginEventType::character_rejected:
      return "character_rejected";
    case LiveLoginEventType::play_forwarded:
      return "play_forwarded";
    case LiveLoginEventType::play_accepted:
      return "play_accepted";
    case LiveLoginEventType::play_rejected:
      return "play_rejected";
    case LiveLoginEventType::client_log_recorded:
      return "client_log_recorded";
    case LiveLoginEventType::client_log_rejected:
      return "client_log_rejected";
    case LiveLoginEventType::key_requested:
      return "key_requested";
    case LiveLoginEventType::malformed:
      return "malformed";
    case LiveLoginEventType::unsupported:
      return "unsupported";
    case LiveLoginEventType::disconnected:
      return "disconnected";
  }

  return "unknown";
}

inline auto format_live_login_event(const LiveLoginEvent& event) -> std::string {
  auto message = std::string("type=") + std::string(live_login_event_type_name(event.type)) +
                 " session=" + std::to_string(event.session.value);
  if (event.protocol_opcode != 0) {
    message += " protocol_opcode=" + std::to_string(event.protocol_opcode);
  }
  if (event.application_opcode != 0) {
    message += " application_opcode=" + std::to_string(event.application_opcode);
  }
  if (event.client_version != 0) {
    message += " client_version=" + std::to_string(event.client_version);
  }
  message += " reply_code=" + std::to_string(static_cast<int>(event.reply_code));
  if (!event.reason.empty()) {
    message += " reason=" + event.reason;
  }
  return message;
}

struct LiveLoginDiagnosticCounters {
  std::size_t events = 0;
  std::size_t active_client_sessions = 0;
  std::size_t registered_worlds = 0;
  std::size_t login_attempts = 0;
  std::size_t login_failures = 0;
  std::size_t malformed_packets = 0;
  std::size_t unsupported_packets = 0;
};

struct LiveLoginOptions {
  std::uint16_t requested_port = 0;
  std::uint16_t login_request_opcode = kLoginRequestAppOpcode;
  std::uint16_t login_reply_opcode = kLoginReplyAppOpcode;
  std::uint16_t world_list_reply_opcode = kWorldListReplyAppOpcode;
  std::uint16_t all_worlds_request_opcode = kAllWorldsRequestAppOpcode;
  std::uint16_t characters_request_opcode = kCharactersRequestAppOpcode;
  std::uint16_t characters_reply_opcode = kCharactersReplyAppOpcode;
  std::uint16_t create_character_request_opcode = kCreateCharacterRequestAppOpcode;
  std::uint16_t create_character_reply_opcode = kCreateCharacterReplyAppOpcode;
  std::uint16_t delete_character_request_opcode = kDeleteCharacterRequestAppOpcode;
  std::uint16_t delete_character_reply_opcode = kDeleteCharacterReplyAppOpcode;
  std::uint16_t play_character_request_opcode = kPlayCharacterRequestAppOpcode;
  std::uint16_t play_character_reply_opcode = kPlayCharacterReplyAppOpcode;
  std::uint16_t client_crashlog_reply_opcode = kClientCrashlogReplyAppOpcode;
  std::uint16_t client_eq2_crashlog_reply_opcode = kClientEq2CrashlogReplyAppOpcode;
  std::uint16_t client_verifylog_reply_opcode = kClientVerifylogReplyAppOpcode;
  std::uint16_t client_alertlog_reply_opcode = kClientAlertlogReplyAppOpcode;
  std::uint16_t client_baselog_reply_opcode = kClientBaselogReplyAppOpcode;
  eq2::protocol::ApplicationOpcodeWidth opcode_width =
      eq2::protocol::ApplicationOpcodeWidth::two_bytes;
  std::uint16_t key_request_opcode = kKeyRequestAppOpcode;
};

class LiveLoginService {
 public:
  LiveLoginService(LoginServerConfig config,
                   eq2::db::LoginAccountRepository& accounts,
                   eq2::protocol::OpcodeVersionRanges supported_versions,
                   LiveLoginOptions options = {})
      : LiveLoginService(std::move(config),
                         accounts,
                         nullptr,
                         nullptr,
                         nullptr,
                         std::move(supported_versions),
                         options) {}

  LiveLoginService(LoginServerConfig config,
                   eq2::db::LoginAccountRepository& accounts,
                   eq2::db::CharacterListRepository& characters,
                   eq2::protocol::OpcodeVersionRanges supported_versions,
                   LiveLoginOptions options = {})
      : LiveLoginService(std::move(config),
                         accounts,
                         nullptr,
                         &characters,
                         nullptr,
                         std::move(supported_versions),
                         options) {}

  LiveLoginService(LoginServerConfig config,
                   eq2::db::LoginAccountRepository& accounts,
                   eq2::db::ClientLogRepository& client_logs,
                   eq2::protocol::OpcodeVersionRanges supported_versions,
                   LiveLoginOptions options = {})
      : LiveLoginService(std::move(config),
                         accounts,
                         nullptr,
                         nullptr,
                         &client_logs,
                         std::move(supported_versions),
                         options) {}

  LiveLoginService(LoginServerConfig config,
                   eq2::db::LoginAccountRepository& accounts,
                   eq2::db::WorldRegistrationRepository& worlds,
                   eq2::protocol::OpcodeVersionRanges supported_versions,
                   LiveLoginOptions options = {})
      : LiveLoginService(std::move(config),
                         accounts,
                         &worlds,
                         nullptr,
                         nullptr,
                         std::move(supported_versions),
                         options) {}

  LiveLoginService(LoginServerConfig config,
                   eq2::db::LoginAccountRepository& accounts,
                   eq2::db::WorldRegistrationRepository& worlds,
                   eq2::db::CharacterListRepository& characters,
                   eq2::protocol::OpcodeVersionRanges supported_versions,
                   LiveLoginOptions options = {})
      : LiveLoginService(std::move(config),
                         accounts,
                         &worlds,
                         &characters,
                         nullptr,
                         std::move(supported_versions),
                         options) {}

  LiveLoginService(LoginServerConfig config,
                   eq2::db::LoginAccountRepository& accounts,
                   eq2::db::WorldRegistrationRepository& worlds,
                   eq2::db::CharacterListRepository& characters,
                   eq2::db::ClientLogRepository& client_logs,
                   eq2::protocol::OpcodeVersionRanges supported_versions,
                   LiveLoginOptions options = {})
      : LiveLoginService(std::move(config),
                         accounts,
                         &worlds,
                         &characters,
                         &client_logs,
                         std::move(supported_versions),
                         options) {}

  LiveLoginService(const LiveLoginService&) = delete;
  auto operator=(const LiveLoginService&) -> LiveLoginService& = delete;

  auto start() -> bool {
    const auto port = options_.requested_port == 0 ? config_.port : options_.requested_port;
    if (!tcp_transport_.start(eq2::net::SocketEndpoint{
        .address = config_.address,
        .port = port,
    })) {
      return false;
    }

    if (!udp_transport_.start(eq2::net::SocketEndpoint{
        .address = config_.address,
        .port = tcp_transport_.port(),
    })) {
      tcp_transport_.stop();
      return false;
    }

    return true;
  }

  void stop() {
    udp_transport_.stop();
    tcp_transport_.stop();
  }

  [[nodiscard]] auto port() const -> std::uint16_t {
    return tcp_transport_.port();
  }

  [[nodiscard]] auto events() const -> std::vector<LiveLoginEvent> {
    std::lock_guard lock(mutex_);
    return events_;
  }

  [[nodiscard]] auto login_outcomes() const -> std::vector<LoginPacketOutcome> {
    std::lock_guard lock(mutex_);
    return login_outcomes_;
  }

  [[nodiscard]] auto world_registration_results() const -> std::vector<WorldRegistrationResult> {
    std::lock_guard lock(mutex_);
    return world_registration_results_;
  }

  [[nodiscard]] auto outbound_packets() const -> std::vector<std::vector<std::uint8_t>> {
    std::lock_guard lock(mutex_);
    return outbound_;
  }

  [[nodiscard]] auto diagnostic_counters() const -> LiveLoginDiagnosticCounters {
    std::lock_guard lock(mutex_);
    auto counters = LiveLoginDiagnosticCounters{
        .events = events_.size(),
        .active_client_sessions = session_account_ids_.size(),
        .registered_worlds = registered_worlds_.size(),
        .login_attempts = login_outcomes_.size(),
    };
    counters.login_failures = static_cast<std::size_t>(
        std::count_if(login_outcomes_.begin(), login_outcomes_.end(), [](const auto& outcome) {
          return outcome.status != LoginPacketStatus::accepted;
        }));
    counters.malformed_packets = static_cast<std::size_t>(
        std::count_if(events_.begin(), events_.end(), [](const auto& event) {
          return event.type == LiveLoginEventType::malformed;
        }));
    counters.unsupported_packets = static_cast<std::size_t>(
        std::count_if(events_.begin(), events_.end(), [](const auto& event) {
          return event.type == LiveLoginEventType::unsupported;
        }));
    return counters;
  }

 private:
  static constexpr auto kMaxBufferedInterserverFrameSize = std::uint32_t{1024U * 1024U};

  struct PendingPlayRequest {
    eq2::net::SessionId client_session;
    eq2::net::TransportKind client_transport = eq2::net::TransportKind::tcp;
    std::int32_t account_id = 0;
    std::int32_t character_id = 0;
    std::int32_t world_id = 0;
    std::int16_t client_version = 0;
  };

  struct PendingCreateRequest {
    eq2::net::SessionId client_session;
    eq2::net::TransportKind client_transport = eq2::net::TransportKind::tcp;
    std::int32_t account_id = 0;
    std::int32_t world_id = 0;
    std::int16_t client_version = 0;
    eq2::protocol::CreateCharacterRequest request;
  };

  LiveLoginService(LoginServerConfig config,
                   eq2::db::LoginAccountRepository& accounts,
                   eq2::db::WorldRegistrationRepository* worlds,
                   eq2::db::CharacterListRepository* characters,
                   eq2::db::ClientLogRepository* client_logs,
                   eq2::protocol::OpcodeVersionRanges supported_versions,
                   LiveLoginOptions options)
      : config_(std::move(config)),
        accounts_(accounts),
        worlds_(worlds),
        characters_(characters),
        client_logs_(client_logs),
        supported_versions_(std::move(supported_versions)),
        options_(options),
        tcp_transport_([this](eq2::net::SessionEvent event) { handle_transport_event(std::move(event)); }),
        udp_transport_([this](eq2::net::SessionEvent event) { handle_transport_event(std::move(event)); }) {}
  void handle_transport_event(eq2::net::SessionEvent event) {
    std::lock_guard lock(mutex_);

    if (event.type == eq2::net::SessionEventType::accepted ||
        event.type == eq2::net::SessionEventType::connected) {
      if (!event.remote_address.empty()) {
        session_remote_addresses_[session_key(event.session, event.transport)] =
            event.remote_address;
      }
      events_.push_back(LiveLoginEvent{
          .type = LiveLoginEventType::transport_accepted,
          .session = event.session,
      });
      return;
    }

    if (event.type == eq2::net::SessionEventType::disconnected) {
      const auto key = session_key(event.session, event.transport);
      if (const auto world_iter = session_world_ids_.find(key);
          world_iter != session_world_ids_.end()) {
        erase_pending_play_for_world(world_iter->second);
        erase_pending_create_for_world(world_iter->second);
        worlds_->record_world_status(world_iter->second, -4, 0, 0, 0);
        remove_registered_world(world_iter->second);
        session_world_ids_.erase(world_iter);
      }
      pipelines_.erase(key);
      world_interserver_buffers_.erase(key);
      erase_pending_play_for_client(event.session, event.transport);
      erase_pending_create_for_client(event.session, event.transport);
      release_session_account(event.session, event.transport);
      session_remote_addresses_.erase(key);
      events_.push_back(LiveLoginEvent{
          .type = LiveLoginEventType::disconnected,
          .session = event.session,
          .reason = event.reason,
      });
      return;
    }

    if (event.type != eq2::net::SessionEventType::received) {
      return;
    }

    if (event.transport == eq2::net::TransportKind::tcp && try_handle_world_registration(event)) {
      return;
    }

    auto& pipeline = pipeline_for(event.session, event.transport);
    auto result = pipeline.receive_datagram(event.bytes);
    std::vector<std::vector<std::uint8_t>> deferred_outbound;
    auto* previous_deferred_outbound = deferred_outbound_;
    const auto previous_deferred_session = deferred_outbound_session_;
    const auto previous_deferred_transport = deferred_outbound_transport_;
    deferred_outbound_ = &deferred_outbound;
    deferred_outbound_session_ = event.session;
    deferred_outbound_transport_ = event.transport;
    for (auto& packet : result.outbound) {
      const auto protocol = eq2::protocol::decode_protocol_packet(packet);
      if (event.transport == eq2::net::TransportKind::tcp && protocol.has_value() &&
          protocol->opcode == eq2::protocol::kOpAck) {
        continue;
      }
      send_or_capture(event.session, event.transport, std::move(packet));
    }

    for (const auto& stream_event : result.events) {
      handle_stream_event(event.session, event.transport, stream_event);
    }
    deferred_outbound_ = previous_deferred_outbound;
    deferred_outbound_session_ = previous_deferred_session;
    deferred_outbound_transport_ = previous_deferred_transport;
    flush_deferred_outbound(event.session, event.transport, std::move(deferred_outbound));
  }

  auto try_handle_world_registration(const eq2::net::SessionEvent& event) -> bool {
    if (worlds_ == nullptr) {
      return false;
    }

    const auto key = session_key(event.session, event.transport);
    auto frames = extract_world_interserver_frames(key, event.bytes);
    if (!frames.has_value()) {
      return false;
    }

    for (const auto& frame_bytes : *frames) {
      handle_world_interserver_frame(event, frame_bytes);
    }
    return true;
  }

  auto handle_world_interserver_frame(const eq2::net::SessionEvent& event,
                                      std::span<const std::uint8_t> bytes) -> bool {
    const auto frame = eq2::protocol::decode_interserver_packet(bytes);
    if (!frame.has_value()) {
      return false;
    }

    const auto key = session_key(event.session, event.transport);
    if (frame->opcode == kServerOpLsInfo) {
      const auto packet = parse_world_registration_packet(bytes);
      if (!packet.has_value()) {
        world_registration_results_.push_back(WorldRegistrationResult{
            .status = WorldRegistrationStatus::rejected_bad_version,
        });
        events_.push_back(LiveLoginEvent{
            .type = LiveLoginEventType::world_rejected,
            .session = event.session,
            .reason = "malformed LSInfo packet",
        });
        return true;
      }

      auto result = register_world_server(*packet, false, *worlds_);
      if (result.status == WorldRegistrationStatus::accepted && result.world.has_value()) {
        cleanup_duplicate_world_sessions(result.world->account_id, key);
        session_world_ids_[key] = result.world->account_id;
        upsert_registered_world(*result.world);
        worlds_->record_world_status(result.world->account_id, 0, 0, 0, 0);
        request_world_support_updates(event.session, event.transport);
        broadcast_world_list_to_clients();
      }
      world_registration_results_.push_back(result);
      events_.push_back(LiveLoginEvent{
          .type = result.status == WorldRegistrationStatus::accepted
                       ? LiveLoginEventType::world_registered
                       : LiveLoginEventType::world_rejected,
          .session = event.session,
          .protocol_opcode = frame->opcode,
          .reason = result.world.has_value() ? result.world->display_name : "",
      });
      return true;
    }

    const auto world_iter = session_world_ids_.find(key);
    if (world_iter == session_world_ids_.end()) {
      world_registration_results_.push_back(WorldRegistrationResult{
          .status = WorldRegistrationStatus::rejected_not_authenticated,
      });
      events_.push_back(LiveLoginEvent{
          .type = LiveLoginEventType::world_rejected,
          .session = event.session,
          .protocol_opcode = frame->opcode,
          .reason = "world interserver packet before LSInfo",
      });
      return true;
    }

    if (frame->opcode == kServerOpKeepAlive) {
      return true;
    }

    const auto payload = eq2::protocol::inflate_interserver_payload(*frame);
    if (!payload.has_value()) {
      events_.push_back(LiveLoginEvent{
          .type = LiveLoginEventType::malformed,
          .session = event.session,
          .protocol_opcode = frame->opcode,
          .reason = "failed to inflate interserver payload",
      });
      return true;
    }

    if (frame->opcode == kServerOpLsStatus) {
      const auto status = eq2::protocol::decode_server_ls_status_payload(*payload);
      if (!status.has_value() || status->player_count > 5000 || status->zone_count > 500) {
        events_.push_back(LiveLoginEvent{
            .type = LiveLoginEventType::world_rejected,
            .session = event.session,
            .protocol_opcode = frame->opcode,
            .reason = "invalid world status packet",
        });
        return true;
      }

      update_registered_world_status(world_iter->second, *status);
      worlds_->record_world_status(world_iter->second,
                                   status->status,
                                   status->player_count,
                                   status->zone_count,
                                   status->world_max_level);
      broadcast_world_list_to_clients();
      events_.push_back(LiveLoginEvent{
          .type = LiveLoginEventType::world_status_updated,
          .session = event.session,
          .protocol_opcode = frame->opcode,
          .reason = std::to_string(world_iter->second),
      });
      return true;
    }

    if (frame->opcode == eq2::protocol::kServerOpCharTimeStamp) {
      handle_world_character_timestamp(event.session, world_iter->second, *payload);
      return true;
    }

    if (frame->opcode == eq2::protocol::kServerOpBasicCharUpdate) {
      handle_world_basic_character_update(event.session, world_iter->second, *payload);
      return true;
    }

    if (frame->opcode == eq2::protocol::kServerOpRaceUpdate) {
      handle_world_race_update(event.session, world_iter->second, *payload);
      return true;
    }

    if (frame->opcode == eq2::protocol::kServerOpNameCharUpdate) {
      handle_world_name_update(event.session, world_iter->second, *payload);
      return true;
    }

    if (frame->opcode == eq2::protocol::kServerOpZoneUpdate) {
      handle_world_zone_update(event.session, world_iter->second, *payload);
      return true;
    }

    if (frame->opcode == eq2::protocol::kServerOpZoneUpdates) {
      handle_world_zone_updates(event.session, world_iter->second, *payload);
      return true;
    }

    if (frame->opcode == eq2::protocol::kServerOpLoginEquipment) {
      handle_world_login_equipment_updates(event.session, world_iter->second, *payload);
      return true;
    }

    if (frame->opcode == eq2::protocol::kServerOpCharacterPicture) {
      handle_world_character_picture_update(event.session, world_iter->second, *payload);
      return true;
    }

    if (frame->opcode == eq2::protocol::kServerOpUserToWorldResponse) {
      handle_user_to_world_response(event.session, event.transport, *payload);
      return true;
    }

    if (frame->opcode == eq2::protocol::kServerOpCharacterCreate) {
      handle_character_create_response(event.session,
                                       event.transport,
                                       world_iter->second,
                                       *payload);
      return true;
    }

    events_.push_back(LiveLoginEvent{
        .type = LiveLoginEventType::unsupported,
        .session = event.session,
        .protocol_opcode = frame->opcode,
        .reason = "unsupported world interserver opcode",
    });
    return true;
  }

  void handle_world_character_timestamp(eq2::net::SessionId session,
                                        std::int32_t world_id,
                                        std::span<const std::uint8_t> payload) {
    const auto update = eq2::protocol::decode_character_timestamp_payload(payload);
    if (!update.has_value()) {
      record_malformed_world_character_update(session,
                                              eq2::protocol::kServerOpCharTimeStamp,
                                              "malformed character timestamp update");
      return;
    }

    const auto updated = characters_ != nullptr &&
                         characters_->update_character_timestamp(update->account_id,
                                                                 update->character_id,
                                                                 world_id,
                                                                 update->unix_timestamp);
    record_world_character_update_result(session,
                                         eq2::protocol::kServerOpCharTimeStamp,
                                         updated,
                                         "character timestamp update");
  }

  void handle_world_basic_character_update(eq2::net::SessionId session,
                                           std::int32_t world_id,
                                           std::span<const std::uint8_t> payload) {
    const auto update = eq2::protocol::decode_char_data_update_payload(payload);
    if (!update.has_value()) {
      record_malformed_world_character_update(session,
                                              eq2::protocol::kServerOpBasicCharUpdate,
                                              "malformed basic character update");
      return;
    }

    auto updated = false;
    if (characters_ != nullptr) {
      switch (update->update_field) {
        case eq2::protocol::kCharUpdateLevelFlag:
          updated = characters_->update_character_level(update->account_id,
                                                        update->character_id,
                                                        world_id,
                                                        update->update_data);
          break;
        case eq2::protocol::kCharUpdateClassFlag:
          updated = characters_->update_character_class(update->account_id,
                                                        update->character_id,
                                                        world_id,
                                                        update->update_data);
          break;
        case eq2::protocol::kCharUpdateGenderFlag:
          updated = characters_->update_character_gender(update->account_id,
                                                         update->character_id,
                                                         world_id,
                                                         update->update_data);
          break;
        case eq2::protocol::kCharUpdateDeleteFlag:
          if (update->update_data == 1) {
            updated = characters_->mark_character_deleted_by_world(update->account_id,
                                                                   update->character_id,
                                                                   world_id);
          }
          break;
        default:
          break;
      }
    }

    record_world_character_update_result(session,
                                         eq2::protocol::kServerOpBasicCharUpdate,
                                         updated,
                                         "basic character update");
  }

  void handle_world_race_update(eq2::net::SessionId session,
                                std::int32_t world_id,
                                std::span<const std::uint8_t> payload) {
    const auto update = eq2::protocol::decode_race_update_payload(payload);
    if (!update.has_value()) {
      record_malformed_world_character_update(session,
                                              eq2::protocol::kServerOpRaceUpdate,
                                              "malformed race update");
      return;
    }

    const auto updated = characters_ != nullptr &&
                         characters_->update_character_race(update->account_id,
                                                            update->character_id,
                                                            world_id,
                                                            update->model_type,
                                                            update->race);
    record_world_character_update_result(session,
                                         eq2::protocol::kServerOpRaceUpdate,
                                         updated,
                                         "race update");
  }

  void handle_world_name_update(eq2::net::SessionId session,
                                std::int32_t world_id,
                                std::span<const std::uint8_t> payload) {
    const auto update = eq2::protocol::decode_char_name_update_payload(payload);
    if (!update.has_value()) {
      record_malformed_world_character_update(session,
                                              eq2::protocol::kServerOpNameCharUpdate,
                                              "malformed name update");
      return;
    }

    const auto updated = characters_ != nullptr &&
                         characters_->update_character_name(update->account_id,
                                                            update->character_id,
                                                            world_id,
                                                            update->name);
    record_world_character_update_result(session,
                                         eq2::protocol::kServerOpNameCharUpdate,
                                         updated,
                                         "name update");
  }

  void handle_world_zone_update(eq2::net::SessionId session,
                                std::int32_t world_id,
                                std::span<const std::uint8_t> payload) {
    const auto update = eq2::protocol::decode_char_zone_update_payload(payload);
    if (!update.has_value()) {
      record_malformed_world_character_update(session,
                                              eq2::protocol::kServerOpZoneUpdate,
                                              "malformed zone update");
      return;
    }

    const auto updated = characters_ != nullptr &&
                         characters_->update_character_zone(update->account_id,
                                                            update->character_id,
                                                            world_id,
                                                            update->zone_id);
    record_world_character_update_result(session,
                                         eq2::protocol::kServerOpZoneUpdate,
                                         updated,
                                         "zone update");
  }

  void handle_world_zone_updates(eq2::net::SessionId session,
                                 std::int32_t world_id,
                                 std::span<const std::uint8_t> payload) {
    const auto updates = eq2::protocol::decode_world_zone_updates_payload(payload);
    if (!updates.has_value()) {
      record_malformed_world_character_update(session,
                                              eq2::protocol::kServerOpZoneUpdates,
                                              "malformed world zone update list");
      return;
    }

    auto records = std::vector<eq2::db::WorldZoneUpdateRecord>{};
    records.reserve(updates->size());
    for (const auto& update : *updates) {
      records.push_back(eq2::db::WorldZoneUpdateRecord{
          .zone_id = update.zone_id,
          .name = std::string(update.name),
          .description = std::string(update.description),
      });
    }

    const auto saved = characters_ != nullptr
                           ? characters_->save_world_zone_updates(world_id, records)
                           : std::size_t{0};
    events_.push_back(LiveLoginEvent{
        .type = saved == records.size() ? LiveLoginEventType::world_status_updated
                                        : LiveLoginEventType::world_rejected,
        .session = session,
        .protocol_opcode = eq2::protocol::kServerOpZoneUpdates,
        .reason = "world zone update list",
    });
  }

  void handle_world_login_equipment_updates(eq2::net::SessionId session,
                                            std::int32_t world_id,
                                            std::span<const std::uint8_t> payload) {
    const auto updates = eq2::protocol::decode_login_equipment_update_payload(payload);
    if (!updates.has_value()) {
      record_malformed_world_character_update(session,
                                              eq2::protocol::kServerOpLoginEquipment,
                                              "malformed login equipment update list");
      return;
    }

    auto records = std::vector<eq2::db::LoginEquipmentUpdateRecord>{};
    records.reserve(updates->size());
    for (const auto& update : *updates) {
      records.push_back(eq2::db::LoginEquipmentUpdateRecord{
          .update_id = update.id,
          .world_character_id = update.world_character_id,
          .equip_type = update.equip_type,
          .red = update.red,
          .green = update.green,
          .blue = update.blue,
          .highlight_red = update.highlight_red,
          .highlight_green = update.highlight_green,
          .highlight_blue = update.highlight_blue,
          .slot = update.slot,
      });
    }

    const auto saved = characters_ != nullptr
                           ? characters_->save_login_equipment_updates(world_id, records)
                           : std::size_t{0};
    events_.push_back(LiveLoginEvent{
        .type = saved == records.size() ? LiveLoginEventType::character_updated
                                        : LiveLoginEventType::character_rejected,
        .session = session,
        .protocol_opcode = eq2::protocol::kServerOpLoginEquipment,
        .reason = "login equipment update list",
    });
  }

  void handle_world_character_picture_update(eq2::net::SessionId session,
                                             std::int32_t world_id,
                                             std::span<const std::uint8_t> payload) {
    const auto update = eq2::protocol::decode_character_picture_update_payload(payload);
    if (!update.has_value()) {
      record_malformed_world_character_update(session,
                                              eq2::protocol::kServerOpCharacterPicture,
                                              "malformed character picture update");
      return;
    }

    const auto saved = characters_ != nullptr &&
                       characters_->save_character_picture(update->account_id,
                                                           update->character_id,
                                                           world_id,
                                                           update->picture);
    events_.push_back(LiveLoginEvent{
        .type = saved ? LiveLoginEventType::character_updated
                      : LiveLoginEventType::character_rejected,
        .session = session,
        .protocol_opcode = eq2::protocol::kServerOpCharacterPicture,
        .reason = "character picture update",
    });
  }

  void record_malformed_world_character_update(eq2::net::SessionId session,
                                               std::uint16_t opcode,
                                               std::string reason) {
    events_.push_back(LiveLoginEvent{
        .type = LiveLoginEventType::malformed,
        .session = session,
        .protocol_opcode = opcode,
        .reason = std::move(reason),
    });
  }

  void record_world_character_update_result(eq2::net::SessionId session,
                                            std::uint16_t opcode,
                                            bool updated,
                                            std::string reason) {
    events_.push_back(LiveLoginEvent{
        .type = updated ? LiveLoginEventType::character_updated
                        : LiveLoginEventType::character_rejected,
        .session = session,
        .protocol_opcode = opcode,
        .reason = std::move(reason),
    });
  }

  auto extract_world_interserver_frames(std::uint64_t key,
                                        std::span<const std::uint8_t> bytes)
      -> std::optional<std::vector<std::vector<std::uint8_t>>> {
    auto& buffer = world_interserver_buffers_[key];
    if (buffer.empty() && !looks_like_interserver_frame(bytes)) {
      world_interserver_buffers_.erase(key);
      return std::nullopt;
    }

    buffer.insert(buffer.end(), bytes.begin(), bytes.end());
    auto frames = std::vector<std::vector<std::uint8_t>>{};
    while (buffer.size() >= sizeof(std::uint32_t)) {
      const auto wire_size = eq2::core::read_u32_le(std::span<const std::uint8_t>(buffer), 0);
      if (wire_size < eq2::protocol::kInterserverPacketHeaderSize ||
          wire_size > kMaxBufferedInterserverFrameSize) {
        world_interserver_buffers_.erase(key);
        return frames.empty()
                   ? std::nullopt
                   : std::optional<std::vector<std::vector<std::uint8_t>>>{std::move(frames)};
      }

      if (buffer.size() < wire_size) {
        break;
      }

      frames.emplace_back(buffer.begin(), buffer.begin() + wire_size);
      buffer.erase(buffer.begin(), buffer.begin() + wire_size);
    }

    if (buffer.empty()) {
      world_interserver_buffers_.erase(key);
    }
    return frames;
  }

  static auto looks_like_interserver_frame(std::span<const std::uint8_t> bytes) -> bool {
    if (bytes.size() < sizeof(std::uint32_t)) {
      return false;
    }

    const auto wire_size = eq2::core::read_u32_le(bytes, 0);
    const auto maybe_protocol_opcode = eq2::core::read_u16_be(bytes, 0);
    if (eq2::protocol::is_protocol_packet_opcode(maybe_protocol_opcode) &&
        wire_size != bytes.size()) {
      return false;
    }
    return wire_size >= eq2::protocol::kInterserverPacketHeaderSize &&
           wire_size <= kMaxBufferedInterserverFrameSize;
  }

  void handle_stream_event(eq2::net::SessionId session,
                           eq2::net::TransportKind transport,
                           const eq2::protocol::StreamEvent& stream_event) {
    switch (stream_event.type) {
      case eq2::protocol::StreamEventType::session_requested:
        events_.push_back(LiveLoginEvent{
            .type = LiveLoginEventType::session_requested,
            .session = session,
            .protocol_opcode = stream_event.protocol_opcode,
        });
        break;
      case eq2::protocol::StreamEventType::server_key_requested:
        send_application_or_capture(session, transport, encode_key_request_app());
        events_.push_back(LiveLoginEvent{
            .type = LiveLoginEventType::key_requested,
            .session = session,
            .protocol_opcode = stream_event.protocol_opcode,
            .application_opcode = options_.key_request_opcode,
        });
        break;
      case eq2::protocol::StreamEventType::app_packet:
        handle_application_packet(session, transport, stream_event);
        break;
      case eq2::protocol::StreamEventType::disconnected:
        pipelines_.erase(session_key(session, transport));
        world_interserver_buffers_.erase(session_key(session, transport));
        erase_pending_play_for_client(session, transport);
        erase_pending_create_for_client(session, transport);
        release_session_account(session, transport);
        session_remote_addresses_.erase(session_key(session, transport));
        events_.push_back(LiveLoginEvent{
            .type = LiveLoginEventType::disconnected,
            .session = session,
            .protocol_opcode = stream_event.protocol_opcode,
        });
        break;
      case eq2::protocol::StreamEventType::malformed:
        events_.push_back(LiveLoginEvent{
            .type = LiveLoginEventType::malformed,
            .session = session,
            .protocol_opcode = stream_event.protocol_opcode,
            .reason = "malformed protocol packet",
        });
        break;
      case eq2::protocol::StreamEventType::unsupported:
      case eq2::protocol::StreamEventType::keep_alive:
        events_.push_back(LiveLoginEvent{
            .type = LiveLoginEventType::unsupported,
            .session = session,
            .protocol_opcode = stream_event.protocol_opcode,
            .reason = "unsupported protocol packet",
        });
        break;
    }
  }

  void handle_application_packet(eq2::net::SessionId session,
                                 eq2::net::TransportKind transport,
                                 const eq2::protocol::StreamEvent& stream_event) {
    if (!stream_event.app_packet.has_value()) {
      events_.push_back(LiveLoginEvent{
          .type = LiveLoginEventType::malformed,
          .session = session,
          .protocol_opcode = stream_event.protocol_opcode,
          .reason = "missing application packet",
      });
      return;
    }

    const auto& packet = *stream_event.app_packet;
    if (packet.opcode == options_.all_worlds_request_opcode) {
      handle_world_description_request(session, transport);
      return;
    }
    if (packet.opcode == options_.characters_request_opcode) {
      handle_character_list_request(session, transport);
      return;
    }
    if (packet.opcode == options_.create_character_request_opcode) {
      handle_create_character_request(session, transport, packet.payload);
      return;
    }
    if (packet.opcode == options_.delete_character_request_opcode) {
      handle_delete_character_request(session, transport, packet.payload);
      return;
    }
    if (packet.opcode == options_.play_character_request_opcode) {
      handle_play_character_request(session, transport, packet.payload);
      return;
    }
    if (client_log_type(packet.opcode).has_value()) {
      handle_client_log_packet(session, transport, packet.opcode, packet.payload);
      return;
    }

    if (packet.opcode != options_.login_request_opcode) {
      events_.push_back(LiveLoginEvent{
          .type = LiveLoginEventType::unsupported,
          .session = session,
          .protocol_opcode = stream_event.protocol_opcode,
          .application_opcode = packet.opcode,
          .reason = "unsupported login application opcode",
      });
      return;
    }

    const auto request = eq2::protocol::parse_legacy_login_request(packet.payload);
    if (!request.has_value()) {
      events_.push_back(LiveLoginEvent{
          .type = LiveLoginEventType::malformed,
          .session = session,
          .protocol_opcode = stream_event.protocol_opcode,
          .application_opcode = packet.opcode,
          .reason = "malformed login request payload",
      });
      return;
    }

    auto authenticated = authenticate_login(
        LoginAuthenticationRequest{
            .credentials =
                LoginCredentials{
                    .username = request->username,
                    .password = request->password,
                },
            .client_version_is_supported =
                supported_versions_.contains_client_version(request->version),
            .account_creation_is_allowed = config_.account_creation_allowed,
            .account_already_has_session = false,
        },
        accounts_);

    auto outcome = LoginPacketOutcome{
        .status = authenticated.reply_code == LoginReplyCode::accepted ? LoginPacketStatus::accepted
                                                                       : LoginPacketStatus::rejected,
        .reply_code = authenticated.reply_code,
        .account = authenticated.account,
        .client_version = request->version,
        .should_disconnect_current_session = authenticated.should_disconnect_current_session,
        .should_disconnect_existing_session = authenticated.should_disconnect_existing_session,
        .should_send_world_list_after_login = authenticated.should_send_world_list_after_login,
    };

    if (outcome.account.has_value()) {
      outcome.should_disconnect_existing_session =
          claim_session_account(session, transport, outcome.account->id);
      const auto key = session_key(session, transport);
      session_client_versions_[key] = outcome.client_version;
      session_account_names_[key] = outcome.account->name;
      const auto remote_iter = session_remote_addresses_.find(key);
      accounts_.record_successful_login(outcome.account->id,
                                        remote_iter == session_remote_addresses_.end()
                                            ? std::string_view{}
                                            : std::string_view{remote_iter->second},
                                        outcome.client_version);
    }

    login_outcomes_.push_back(outcome);
    send_application_or_capture(session, transport, encode_login_reply_app(outcome));
    events_.push_back(LiveLoginEvent{
        .type = outcome.status == LoginPacketStatus::accepted ? LiveLoginEventType::login_accepted
                                                              : LiveLoginEventType::login_rejected,
        .session = session,
        .protocol_opcode = stream_event.protocol_opcode,
        .application_opcode = packet.opcode,
        .reply_code = outcome.reply_code,
        .client_version = outcome.client_version,
    });
  }

  void handle_client_log_packet(eq2::net::SessionId session,
                                eq2::net::TransportKind transport,
                                std::uint16_t opcode,
                                std::span<const std::uint8_t> payload) {
    const auto type = client_log_type(opcode);
    const auto key = session_key(session, transport);
    const auto version_iter = session_client_versions_.find(key);
    if (!type.has_value() || version_iter == session_client_versions_.end()) {
      events_.push_back(LiveLoginEvent{
          .type = LiveLoginEventType::client_log_rejected,
          .session = session,
          .application_opcode = opcode,
          .reason = "client log before login",
      });
      return;
    }

    const auto decoded = eq2::protocol::decode_client_log_payload(payload, version_iter->second);
    if (!decoded.has_value()) {
      events_.push_back(LiveLoginEvent{
          .type = LiveLoginEventType::client_log_rejected,
          .session = session,
          .application_opcode = opcode,
          .client_version = version_iter->second,
          .reason = "malformed client log packet",
      });
      return;
    }

    auto account_name = std::string{};
    if (const auto account_iter = session_account_names_.find(key);
        account_iter != session_account_names_.end()) {
      account_name = account_iter->second;
    }

    if (client_logs_ != nullptr) {
      client_logs_->save_client_log(eq2::db::ClientLogRecord{
          .type = std::string(*type),
          .message = decoded->message,
          .account_name = account_name,
          .client_version = version_iter->second,
      });
    }

    events_.push_back(LiveLoginEvent{
        .type = LiveLoginEventType::client_log_recorded,
        .session = session,
        .application_opcode = opcode,
        .client_version = version_iter->second,
        .reason = std::string(*type),
    });
  }

  void handle_world_description_request(eq2::net::SessionId session,
                                        eq2::net::TransportKind transport) {
    const auto key = session_key(session, transport);
    const auto account_iter = session_account_ids_.find(key);
    const auto version_iter = session_client_versions_.find(key);
    if (account_iter == session_account_ids_.end() ||
        version_iter == session_client_versions_.end()) {
      events_.push_back(LiveLoginEvent{
          .type = LiveLoginEventType::unsupported,
          .session = session,
          .application_opcode = options_.all_worlds_request_opcode,
          .reason = "world description request before login",
      });
      return;
    }

    send_application_or_capture(session, transport, encode_world_list_reply_app(version_iter->second));
    send_application_or_capture(
        session,
        transport,
        encode_character_list_reply_app(static_cast<std::uint32_t>(account_iter->second),
                                        version_iter->second));
    send_application_or_capture(
        session,
        transport,
        encode_login_reply_app(static_cast<std::uint8_t>(10), 0, version_iter->second));
  }

  void handle_character_list_request(eq2::net::SessionId session,
                                     eq2::net::TransportKind transport) {
    const auto key = session_key(session, transport);
    const auto account_iter = session_account_ids_.find(key);
    const auto version_iter = session_client_versions_.find(key);
    if (account_iter == session_account_ids_.end() ||
        version_iter == session_client_versions_.end()) {
      events_.push_back(LiveLoginEvent{
          .type = LiveLoginEventType::unsupported,
          .session = session,
          .application_opcode = options_.characters_request_opcode,
          .reason = "character list request before login",
      });
      return;
    }

    send_application_or_capture(
        session,
        transport,
        encode_character_list_reply_app(static_cast<std::uint32_t>(account_iter->second),
                                        version_iter->second));
  }

  void handle_create_character_request(eq2::net::SessionId session,
                                       eq2::net::TransportKind transport,
                                       std::span<const std::uint8_t> payload) {
    const auto key = session_key(session, transport);
    const auto account_iter = session_account_ids_.find(key);
    const auto version_iter = session_client_versions_.find(key);
    if (account_iter == session_account_ids_.end() ||
        version_iter == session_client_versions_.end()) {
      events_.push_back(LiveLoginEvent{
          .type = LiveLoginEventType::unsupported,
          .session = session,
          .application_opcode = options_.create_character_request_opcode,
          .reason = "create character request before login",
      });
      return;
    }

    const auto request = eq2::protocol::parse_create_character_request(
        payload, static_cast<std::uint16_t>(version_iter->second));
    if (!request.has_value()) {
      events_.push_back(LiveLoginEvent{
          .type = LiveLoginEventType::malformed,
          .session = session,
          .application_opcode = options_.create_character_request_opcode,
          .reason = "malformed create character request",
      });
      return;
    }

    const auto world_session = find_world_session(request->server_id);
    if (!world_session.has_value()) {
      events_.push_back(LiveLoginEvent{
          .type = LiveLoginEventType::character_create_rejected,
          .session = session,
          .application_opcode = options_.create_character_request_opcode,
          .reason = "create requested for unavailable world",
      });
      return;
    }

    const auto forward_payload = eq2::protocol::encode_create_character_forward_payload(
        payload, static_cast<std::uint16_t>(version_iter->second), account_iter->second);
    if (!forward_payload.has_value()) {
      events_.push_back(LiveLoginEvent{
          .type = LiveLoginEventType::malformed,
          .session = session,
          .application_opcode = options_.create_character_request_opcode,
          .reason = "malformed create character forward payload",
      });
      return;
    }

    const auto frame = eq2::protocol::encode_interserver_packet(
        eq2::protocol::kServerOpCharacterCreate, forward_payload->bytes);
    if (!frame.has_value()) {
      events_.push_back(LiveLoginEvent{
          .type = LiveLoginEventType::character_create_rejected,
          .session = session,
          .application_opcode = options_.create_character_request_opcode,
          .reason = "failed to encode create character interserver packet",
      });
      return;
    }

    erase_pending_create_for_client(session, transport);
    pending_create_requests_.push_back(PendingCreateRequest{
        .client_session = session,
        .client_transport = transport,
        .account_id = account_iter->second,
        .world_id = request->server_id,
        .client_version = version_iter->second,
        .request = *request,
    });
    send_or_capture(world_session->first, world_session->second, *frame);
    events_.push_back(LiveLoginEvent{
        .type = LiveLoginEventType::character_create_forwarded,
        .session = session,
        .application_opcode = options_.create_character_request_opcode,
        .reason = std::to_string(request->server_id),
    });
  }

  void handle_delete_character_request(eq2::net::SessionId session,
                                       eq2::net::TransportKind transport,
                                       std::span<const std::uint8_t> payload) {
    const auto key = session_key(session, transport);
    const auto account_iter = session_account_ids_.find(key);
    const auto version_iter = session_client_versions_.find(key);
    if (account_iter == session_account_ids_.end() ||
        version_iter == session_client_versions_.end()) {
      events_.push_back(LiveLoginEvent{
          .type = LiveLoginEventType::unsupported,
          .session = session,
          .application_opcode = options_.delete_character_request_opcode,
          .reason = "delete character request before login",
      });
      return;
    }

    const auto request = eq2::protocol::parse_delete_character_request(payload);
    if (!request.has_value()) {
      events_.push_back(LiveLoginEvent{
          .type = LiveLoginEventType::malformed,
          .session = session,
          .application_opcode = options_.delete_character_request_opcode,
          .reason = "malformed delete character request",
      });
      return;
    }

    const auto deleted = characters_ != nullptr &&
                         characters_->mark_character_deleted(account_iter->second,
                                                             request->character_id,
                                                             request->server_id,
                                                             request->character_name);
    if (deleted) {
      send_delete_character_to_world(*request, account_iter->second);
    }
    const auto response_payload = eq2::protocol::encode_delete_character_response_payload(
        eq2::protocol::DeleteCharacterResponse{
            .response = deleted ? std::uint8_t{1} : std::uint8_t{0},
            .server_id = request->server_id,
            .character_id = request->character_id,
            .character_name = request->character_name,
        });
    send_application_or_capture(
        session,
        transport,
        eq2::protocol::encode_application_packet(options_.delete_character_reply_opcode,
                                                 response_payload,
                                                 options_.opcode_width));
    send_application_or_capture(
        session,
        transport,
        encode_character_list_reply_app(static_cast<std::uint32_t>(account_iter->second),
                                        version_iter->second));
    events_.push_back(LiveLoginEvent{
        .type = deleted ? LiveLoginEventType::character_deleted
                        : LiveLoginEventType::character_rejected,
        .session = session,
        .application_opcode = options_.delete_character_request_opcode,
        .reason = "delete character request",
    });
  }

  void send_delete_character_to_world(const eq2::protocol::DeleteCharacterRequest& request,
                                      std::int32_t account_id) {
    const auto world_session = find_world_session(request.server_id);
    if (!world_session.has_value()) {
      return;
    }

    const auto payload = eq2::protocol::encode_char_data_update_payload(
        eq2::protocol::CharDataUpdate{
            .account_id = account_id,
            .character_id = request.character_id,
            .update_field = eq2::protocol::kCharUpdateDeleteFlag,
            .update_data = 1,
        });
    const auto frame = eq2::protocol::encode_interserver_packet(
        eq2::protocol::kServerOpBasicCharUpdate, payload);
    if (!frame.has_value()) {
      return;
    }

    send_or_capture(world_session->first, world_session->second, *frame);
  }

  void handle_play_character_request(eq2::net::SessionId session,
                                     eq2::net::TransportKind transport,
                                     std::span<const std::uint8_t> payload) {
    const auto key = session_key(session, transport);
    const auto account_iter = session_account_ids_.find(key);
    const auto version_iter = session_client_versions_.find(key);
    if (account_iter == session_account_ids_.end() ||
        version_iter == session_client_versions_.end()) {
      events_.push_back(LiveLoginEvent{
          .type = LiveLoginEventType::unsupported,
          .session = session,
          .application_opcode = options_.play_character_request_opcode,
          .reason = "play character request before login",
      });
      return;
    }

    const auto request = eq2::protocol::parse_play_character_request(
        payload, static_cast<std::uint16_t>(version_iter->second));
    if (!request.has_value()) {
      events_.push_back(LiveLoginEvent{
          .type = LiveLoginEventType::malformed,
          .session = session,
          .application_opcode = options_.play_character_request_opcode,
          .reason = "malformed play character request",
      });
      return;
    }

    const auto character = find_play_character(account_iter->second,
                                               *request,
                                               version_iter->second);
    if (!character.has_value()) {
      send_play_failed(session, transport, account_iter->second, version_iter->second, 0);
      events_.push_back(LiveLoginEvent{
          .type = LiveLoginEventType::play_rejected,
          .session = session,
          .application_opcode = options_.play_character_request_opcode,
          .reason = "invalid play character request",
      });
      return;
    }

    const auto world_session = find_world_session(character->server_id);
    if (!world_session.has_value()) {
      send_play_failed(session, transport, account_iter->second, version_iter->second, 0);
      events_.push_back(LiveLoginEvent{
          .type = LiveLoginEventType::play_rejected,
          .session = session,
          .application_opcode = options_.play_character_request_opcode,
          .reason = "play requested for unavailable world",
      });
      return;
    }

    const auto remote_iter = session_remote_addresses_.find(key);
    const auto request_payload = eq2::protocol::encode_user_to_world_request_payload(
        eq2::protocol::UserToWorldRequest{
            .login_account_id = account_iter->second,
            .character_id = character->character_id,
            .world_id = character->server_id,
            .from_id = 0,
            .to_id = 0,
            .ip_address = remote_iter == session_remote_addresses_.end()
                              ? std::string_view{"0.0.0.0"}
                              : std::string_view{remote_iter->second},
        });
    const auto frame = eq2::protocol::encode_interserver_packet(
        eq2::protocol::kServerOpUserToWorldRequest, request_payload);
    if (!frame.has_value()) {
      send_play_failed(session, transport, account_iter->second, version_iter->second, 0);
      return;
    }

    pending_play_requests_.push_back(PendingPlayRequest{
        .client_session = session,
        .client_transport = transport,
        .account_id = account_iter->second,
        .character_id = character->character_id,
        .world_id = character->server_id,
        .client_version = version_iter->second,
    });
    send_or_capture(world_session->first, world_session->second, *frame);
    events_.push_back(LiveLoginEvent{
        .type = LiveLoginEventType::play_forwarded,
        .session = session,
        .application_opcode = options_.play_character_request_opcode,
        .reason = std::to_string(character->server_id),
    });
  }

  void handle_user_to_world_response(eq2::net::SessionId session,
                                     eq2::net::TransportKind transport,
                                     std::span<const std::uint8_t> payload) {
    const auto response = eq2::protocol::decode_user_to_world_response_payload(payload);
    if (!response.has_value()) {
      events_.push_back(LiveLoginEvent{
          .type = LiveLoginEventType::malformed,
          .session = session,
          .protocol_opcode = eq2::protocol::kServerOpUserToWorldResponse,
          .reason = "malformed user-to-world response",
      });
      return;
    }

    const auto pending_iter =
        std::find_if(pending_play_requests_.begin(),
                     pending_play_requests_.end(),
                     [&](const auto& pending) {
                       return pending.account_id == response->login_account_id &&
                              pending.character_id == response->character_id &&
                              pending.world_id == response->world_id;
                     });
    if (pending_iter == pending_play_requests_.end()) {
      events_.push_back(LiveLoginEvent{
          .type = LiveLoginEventType::play_rejected,
          .session = session,
          .protocol_opcode = eq2::protocol::kServerOpUserToWorldResponse,
          .reason = "user-to-world response without pending client",
      });
      return;
    }

    const auto pending = *pending_iter;
    pending_play_requests_.erase(pending_iter);

    const auto payload_out = eq2::protocol::encode_play_character_response_payload(
        eq2::protocol::PlayCharacterResponse{
            .response = response->response,
            .server = response->response == 1 ? std::string(response->ip_address) : std::string{},
            .port = response->response == 1 ? static_cast<std::uint16_t>(response->port)
                                             : std::uint16_t{0},
            .account_id = response->response == 1 ? response->login_account_id : pending.account_id,
            .access_code = response->response == 1 ? response->access_key : 0,
        },
        static_cast<std::uint16_t>(pending.client_version));
    send_application_or_capture(
        pending.client_session,
        pending.client_transport,
        eq2::protocol::encode_application_packet(options_.play_character_reply_opcode,
                                                 payload_out,
                                                 options_.opcode_width));
    events_.push_back(LiveLoginEvent{
        .type = response->response == 1 ? LiveLoginEventType::play_accepted
                                        : LiveLoginEventType::play_rejected,
        .session = pending.client_session,
        .protocol_opcode = eq2::protocol::kServerOpUserToWorldResponse,
        .application_opcode = options_.play_character_reply_opcode,
        .reason = std::to_string(response->world_id),
    });
    (void)transport;
  }

  void handle_character_create_response(eq2::net::SessionId session,
                                        eq2::net::TransportKind transport,
                                        std::int32_t world_id,
                                        std::span<const std::uint8_t> payload) {
    const auto response = eq2::protocol::decode_create_character_response_payload(payload);
    if (!response.has_value()) {
      events_.push_back(LiveLoginEvent{
          .type = LiveLoginEventType::malformed,
          .session = session,
          .protocol_opcode = eq2::protocol::kServerOpCharacterCreate,
          .reason = "malformed create character response",
      });
      return;
    }

    const auto pending_iter =
        std::find_if(pending_create_requests_.begin(),
                     pending_create_requests_.end(),
                     [&](const auto& pending) {
                       if (pending.world_id != world_id) {
                         return false;
                       }
                       return response->account_id == 0 ||
                              pending.account_id == response->account_id;
                     });
    if (pending_iter == pending_create_requests_.end()) {
      events_.push_back(LiveLoginEvent{
          .type = LiveLoginEventType::character_create_rejected,
          .session = session,
          .protocol_opcode = eq2::protocol::kServerOpCharacterCreate,
          .reason = "create character response without pending client",
      });
      (void)transport;
      return;
    }

    const auto pending = *pending_iter;
    pending_create_requests_.erase(pending_iter);

    if (response->response == 1 && characters_ != nullptr) {
      characters_->save_created_character(eq2::db::CreatedCharacterRecord{
          .account_id = pending.account_id,
          .server_id = pending.world_id,
          .character_id = response->character_id,
          .name = pending.request.character_name,
          .race = pending.request.race,
          .character_class = pending.request.character_class,
          .gender = pending.request.gender,
          .deity = pending.request.deity,
          .level = pending.request.level,
          .body_size = pending.request.body_size,
          .body_age = pending.request.body_age,
          .appearance_files =
              eq2::db::CreatedCharacterAppearanceFiles{
                  .race_file = pending.request.appearance_files.race_file,
                  .soga_race_file = pending.request.appearance_files.soga_race_file,
                  .hair_file = pending.request.appearance_files.hair_file,
                  .soga_hair_file = pending.request.appearance_files.soga_hair_file,
                  .face_file = pending.request.appearance_files.face_file,
                  .soga_face_file = pending.request.appearance_files.soga_face_file,
                  .chest_file = pending.request.appearance_files.chest_file,
                  .soga_chest_file = pending.request.appearance_files.soga_chest_file,
                  .legs_file = pending.request.appearance_files.legs_file,
                  .soga_legs_file = pending.request.appearance_files.soga_legs_file,
                  .wing_file = pending.request.appearance_files.wing_file,
                  .soga_wing_file = pending.request.appearance_files.soga_wing_file,
              },
          .appearance_values = create_character_appearance_values(pending.request),
      });
    }

    const auto reply_payload = eq2::protocol::encode_create_character_reply_payload(
        eq2::protocol::CreateCharacterReply{
            .account_id = pending.account_id,
            .response = response->response,
            .character_name = response->response == 1 ? pending.request.character_name
                                                       : std::string{},
        },
        static_cast<std::uint16_t>(pending.client_version));
    send_application_or_capture(
        pending.client_session,
        pending.client_transport,
        eq2::protocol::encode_application_packet(options_.create_character_reply_opcode,
                                                 reply_payload,
                                                 options_.opcode_width));

    if (response->response == 1) {
      send_application_or_capture(
          pending.client_session,
          pending.client_transport,
          encode_character_list_reply_app(static_cast<std::uint32_t>(pending.account_id),
                                          pending.client_version));
      if (pending.client_version <= 561) {
        send_create_play_request_to_world(pending, response->character_id, session, transport);
      }
    }

    events_.push_back(LiveLoginEvent{
        .type = response->response == 1 ? LiveLoginEventType::character_create_accepted
                                        : LiveLoginEventType::character_create_rejected,
        .session = pending.client_session,
        .protocol_opcode = eq2::protocol::kServerOpCharacterCreate,
        .application_opcode = options_.create_character_reply_opcode,
        .reason = std::to_string(pending.world_id),
    });
  }

  void send_create_play_request_to_world(const PendingCreateRequest& pending,
                                         std::int32_t character_id,
                                         eq2::net::SessionId world_session,
                                         eq2::net::TransportKind world_transport) {
    const auto key = session_key(pending.client_session, pending.client_transport);
    const auto remote_iter = session_remote_addresses_.find(key);
    const auto request_payload = eq2::protocol::encode_user_to_world_request_payload(
        eq2::protocol::UserToWorldRequest{
            .login_account_id = pending.account_id,
            .character_id = character_id,
            .world_id = pending.world_id,
            .from_id = 0,
            .to_id = 0,
            .ip_address = remote_iter == session_remote_addresses_.end()
                              ? std::string_view{"0.0.0.0"}
                              : std::string_view{remote_iter->second},
        });
    const auto frame = eq2::protocol::encode_interserver_packet(
        eq2::protocol::kServerOpUserToWorldRequest, request_payload);
    if (!frame.has_value()) {
      return;
    }

    pending_play_requests_.push_back(PendingPlayRequest{
        .client_session = pending.client_session,
        .client_transport = pending.client_transport,
        .account_id = pending.account_id,
        .character_id = character_id,
        .world_id = pending.world_id,
        .client_version = pending.client_version,
    });
    send_or_capture(world_session, world_transport, *frame);
  }

  [[nodiscard]] auto find_play_character(std::int32_t account_id,
                                         const eq2::protocol::PlayCharacterRequest& request,
                                         std::int16_t client_version) const
      -> std::optional<eq2::db::CharacterListRecord> {
    if (characters_ == nullptr) {
      return std::nullopt;
    }

    for (const auto& character : characters_->load_character_list(account_id)) {
      if (character.character_id != request.character_id) {
        continue;
      }
      if (client_version < eq2::protocol::kModernPlayCharacterRequestVersion) {
        if (character.name != request.character_name) {
          continue;
        }
      } else if (character.server_id != request.server_id) {
        continue;
      }
      if (!registered_world_is_available(character.server_id)) {
        continue;
      }
      return character;
    }

    return std::nullopt;
  }

  [[nodiscard]] auto find_world_session(std::int32_t world_id) const
      -> std::optional<std::pair<eq2::net::SessionId, eq2::net::TransportKind>> {
    const auto iter =
        std::find_if(session_world_ids_.begin(), session_world_ids_.end(), [world_id](const auto& entry) {
          return entry.second == world_id;
        });
    if (iter == session_world_ids_.end()) {
      return std::nullopt;
    }

    return decode_session_key(iter->first);
  }

  void request_world_support_updates(eq2::net::SessionId session,
                                     eq2::net::TransportKind transport) {
    send_interserver_frame(session,
                           transport,
                           eq2::protocol::kServerOpZoneUpdates,
                           eq2::protocol::encode_zone_update_request_payload());
    send_interserver_frame(session,
                           transport,
                           eq2::protocol::kServerOpLoginEquipment,
                           eq2::protocol::encode_equipment_update_request_payload());
  }

  void send_interserver_frame(eq2::net::SessionId session,
                              eq2::net::TransportKind transport,
                              std::uint16_t opcode,
                              std::span<const std::uint8_t> payload) {
    const auto frame = eq2::protocol::encode_interserver_packet(opcode, payload);
    if (!frame.has_value()) {
      return;
    }

    send_or_capture(session, transport, *frame);
  }

  void broadcast_world_list_to_clients() {
    auto clients = std::vector<std::tuple<eq2::net::SessionId,
                                          eq2::net::TransportKind,
                                          std::int16_t>>{};
    clients.reserve(session_account_ids_.size());
    for (const auto& [key, _] : session_account_ids_) {
      const auto version_iter = session_client_versions_.find(key);
      if (version_iter == session_client_versions_.end()) {
        continue;
      }

      const auto [session, transport] = decode_session_key(key);
      clients.push_back({session, transport, version_iter->second});
    }

    for (const auto& [session, transport, version] : clients) {
      send_application_or_capture(session, transport, encode_world_list_reply_app(version));
    }
  }

  void cleanup_duplicate_world_sessions(std::int32_t world_id, std::uint64_t keep_key) {
    auto duplicate_keys = std::vector<std::uint64_t>{};
    for (const auto& [key, registered_world_id] : session_world_ids_) {
      if (key != keep_key && registered_world_id == world_id) {
        duplicate_keys.push_back(key);
      }
    }

    for (const auto key : duplicate_keys) {
      const auto [session, transport] = decode_session_key(key);
      session_world_ids_.erase(key);
      world_interserver_buffers_.erase(key);
      if (transport == eq2::net::TransportKind::tcp) {
        tcp_transport_.disconnect(session);
      }
      events_.push_back(LiveLoginEvent{
          .type = LiveLoginEventType::world_rejected,
          .session = session,
          .reason = "duplicate world session replaced",
      });
    }
  }

  void send_play_failed(eq2::net::SessionId session,
                        eq2::net::TransportKind transport,
                        std::int32_t account_id,
                        std::int16_t client_version,
                        std::uint8_t response_code) {
    const auto payload = eq2::protocol::encode_play_character_response_payload(
        eq2::protocol::PlayCharacterResponse{
            .response = response_code,
            .server = "",
            .port = 0,
            .account_id = account_id,
            .access_code = 0,
        },
        static_cast<std::uint16_t>(client_version));
    send_application_or_capture(
        session,
        transport,
        eq2::protocol::encode_application_packet(options_.play_character_reply_opcode,
                                                 payload,
                                                 options_.opcode_width));
  }

  void send_or_capture(eq2::net::SessionId session,
                       eq2::net::TransportKind transport,
                       std::vector<std::uint8_t> packet) {
    if (deferred_outbound_ != nullptr && deferred_outbound_session_.has_value() &&
        deferred_outbound_transport_.has_value() && *deferred_outbound_session_ == session &&
        *deferred_outbound_transport_ == transport) {
      deferred_outbound_->push_back(std::move(packet));
      return;
    }

    send_protocol_packet_now(session, transport, std::move(packet));
  }

  void send_protocol_packet_now(eq2::net::SessionId session,
                                eq2::net::TransportKind transport,
                                std::vector<std::uint8_t> packet) {
    if (transport == eq2::net::TransportKind::udp) {
      udp_transport_.send(session, packet);
    } else {
      tcp_transport_.send(session, packet);
    }
    outbound_.push_back(std::move(packet));
  }

  void flush_deferred_outbound(eq2::net::SessionId session,
                               eq2::net::TransportKind transport,
                               std::vector<std::vector<std::uint8_t>> packets) {
    if (packets.empty()) {
      return;
    }

    if (transport == eq2::net::TransportKind::udp) {
      if (auto combined = eq2::protocol::coalesce_legacy_protocol_datagrams(packets)) {
        send_protocol_packet_now(session, transport, std::move(*combined));
        return;
      }
    }

    for (auto& packet : packets) {
      send_protocol_packet_now(session, transport, std::move(packet));
    }
  }

  void send_application_or_capture(eq2::net::SessionId session,
                                   eq2::net::TransportKind transport,
                                   std::vector<std::uint8_t> app_packet) {
    const auto key = session_key(session, transport);
    auto iter = pipelines_.find(key);
    const auto protocol_packet = iter == pipelines_.end()
                                     ? eq2::protocol::encode_protocol_packet(
                                           eq2::protocol::kOpPacket, app_packet)
                                     : iter->second.encode_application_protocol_packet(app_packet);
    send_or_capture(session, transport, protocol_packet);
  }

  [[nodiscard]] auto encode_key_request_app() const -> std::vector<std::uint8_t> {
    eq2::protocol::PacketWriter payload;
    payload.append_u32_le(60);
    for (auto index = 0; index < 60; ++index) {
      payload.append_u8(0xff);
    }
    payload.append_u32_le(1);
    payload.append_u8(1);
    return eq2::protocol::encode_application_packet(
        options_.key_request_opcode, payload.bytes(), options_.opcode_width);
  }

  auto pipeline_for(eq2::net::SessionId session, eq2::net::TransportKind transport)
      -> eq2::protocol::StreamPipeline& {
    auto [iter, inserted] = pipelines_.try_emplace(
        session_key(session, transport),
        eq2::protocol::StreamPipelineOptions{
            .application_opcode_width = options_.opcode_width,
        });
    (void)inserted;
    return iter->second;
  }

  void upsert_registered_world(const RegisteredWorld& world) {
    for (auto& registered : registered_worlds_) {
      if (registered.account_id == world.account_id) {
        registered = world;
        return;
      }
    }

    registered_worlds_.push_back(world);
  }

  void update_registered_world_status(std::int32_t world_id,
                                      const eq2::protocol::ServerLsStatus& status) {
    for (auto& registered : registered_worlds_) {
      if (registered.account_id == world_id) {
        registered.status = status.status;
        registered.player_count = status.player_count;
        registered.zone_count = status.zone_count;
        registered.world_max_level = status.world_max_level;
        return;
      }
    }
  }

  void remove_registered_world(std::int32_t world_id) {
    registered_worlds_.erase(
        std::remove_if(registered_worlds_.begin(),
                       registered_worlds_.end(),
                       [world_id](const auto& world) { return world.account_id == world_id; }),
        registered_worlds_.end());
  }

  [[nodiscard]] auto registered_world_is_available(std::int32_t world_id) const -> bool {
    if (worlds_ == nullptr) {
      return true;
    }

    return std::any_of(registered_worlds_.begin(),
                       registered_worlds_.end(),
                       [world_id](const auto& world) {
                         return world.account_id == world_id &&
                                (world.status >= 0 || world.status == -2);
                       });
  }

  [[nodiscard]] auto session_key(eq2::net::SessionId session,
                                 eq2::net::TransportKind transport) const -> std::uint64_t {
    return transport == eq2::net::TransportKind::udp ? (1ULL << 63U) | session.value
                                                     : session.value;
  }

  static auto create_character_appearance_values(
      const eq2::protocol::CreateCharacterRequest& request)
      -> std::vector<eq2::db::CharacterAppearanceRecord> {
    auto values = std::vector<eq2::db::CharacterAppearanceRecord>{};
    values.reserve(request.appearance_values.size());
    for (const auto& value : request.appearance_values) {
      values.push_back(eq2::db::CharacterAppearanceRecord{
          .type = value.type,
          .signed_value = value.signed_value,
          .red = value.red,
          .green = value.green,
          .blue = value.blue,
      });
    }
    return values;
  }

  static auto decode_session_key(std::uint64_t key)
      -> std::pair<eq2::net::SessionId, eq2::net::TransportKind> {
    constexpr auto udp_mask = 1ULL << 63U;
    const auto transport = (key & udp_mask) != 0 ? eq2::net::TransportKind::udp
                                                 : eq2::net::TransportKind::tcp;
    return std::pair<eq2::net::SessionId, eq2::net::TransportKind>{
        eq2::net::SessionId{key & ~udp_mask},
        transport,
    };
  }

  auto claim_session_account(eq2::net::SessionId session,
                             eq2::net::TransportKind transport,
                             std::int32_t account_id) -> bool {
    const auto key = session_key(session, transport);
    const auto session_iter = session_account_ids_.find(key);
    if (session_iter != session_account_ids_.end() && session_iter->second == account_id) {
      const auto count_iter = active_account_refcounts_.find(account_id);
      return count_iter != active_account_refcounts_.end() && count_iter->second > 1;
    }

    if (session_iter != session_account_ids_.end()) {
      release_session_account(session, transport);
    }

    const auto already_active = active_account_refcounts_.contains(account_id);
    ++active_account_refcounts_[account_id];
    session_account_ids_[key] = account_id;
    return already_active;
  }

  void release_session_account(eq2::net::SessionId session,
                               eq2::net::TransportKind transport) {
    const auto key = session_key(session, transport);
    const auto session_iter = session_account_ids_.find(key);
    if (session_iter == session_account_ids_.end()) {
      return;
    }

    const auto account_id = session_iter->second;
    session_account_ids_.erase(session_iter);
    session_client_versions_.erase(key);
    session_account_names_.erase(key);

    const auto count_iter = active_account_refcounts_.find(account_id);
    if (count_iter == active_account_refcounts_.end()) {
      return;
    }

    if (count_iter->second <= 1) {
      active_account_refcounts_.erase(count_iter);
      return;
    }

    --count_iter->second;
  }

  [[nodiscard]] auto client_log_type(std::uint16_t opcode) const
      -> std::optional<std::string_view> {
    if (opcode == options_.client_crashlog_reply_opcode) {
      return std::string_view{"Crash Log"};
    }
    if (opcode == options_.client_eq2_crashlog_reply_opcode) {
      return std::string_view{"EQ2 Crash Log"};
    }
    if (opcode == options_.client_verifylog_reply_opcode) {
      return std::string_view{"Verify Log"};
    }
    if (opcode == options_.client_alertlog_reply_opcode) {
      return std::string_view{"Alert Log"};
    }
    if (opcode == options_.client_baselog_reply_opcode) {
      return std::string_view{"Base Log"};
    }
    return std::nullopt;
  }

  void erase_pending_play_for_client(eq2::net::SessionId session,
                                     eq2::net::TransportKind transport) {
    pending_play_requests_.erase(
        std::remove_if(pending_play_requests_.begin(),
                       pending_play_requests_.end(),
                       [session, transport](const auto& pending) {
                         return pending.client_session.value == session.value &&
                                pending.client_transport == transport;
                       }),
        pending_play_requests_.end());
  }

  void erase_pending_play_for_world(std::int32_t world_id) {
    pending_play_requests_.erase(
        std::remove_if(pending_play_requests_.begin(),
                       pending_play_requests_.end(),
                       [world_id](const auto& pending) {
                         return pending.world_id == world_id;
                       }),
        pending_play_requests_.end());
  }

  void erase_pending_create_for_client(eq2::net::SessionId session,
                                       eq2::net::TransportKind transport) {
    pending_create_requests_.erase(
        std::remove_if(pending_create_requests_.begin(),
                       pending_create_requests_.end(),
                       [session, transport](const auto& pending) {
                         return pending.client_session.value == session.value &&
                                pending.client_transport == transport;
                       }),
        pending_create_requests_.end());
  }

  void erase_pending_create_for_world(std::int32_t world_id) {
    pending_create_requests_.erase(
        std::remove_if(pending_create_requests_.begin(),
                       pending_create_requests_.end(),
                       [world_id](const auto& pending) {
                         return pending.world_id == world_id;
                       }),
        pending_create_requests_.end());
  }

  [[nodiscard]] auto encode_login_reply_app(const LoginPacketOutcome& outcome) const
      -> std::vector<std::uint8_t> {
    return encode_login_reply_app(
        static_cast<std::uint8_t>(outcome.reply_code),
        outcome.account.has_value() ? static_cast<std::uint32_t>(outcome.account->id) : 0,
        outcome.client_version);
  }

  [[nodiscard]] auto encode_login_reply_app(std::uint8_t reply_code,
                                            std::uint32_t account_id,
                                            std::int16_t client_version) const
      -> std::vector<std::uint8_t> {
    const auto payload = eq2::protocol::encode_login_reply_payload(
        eq2::protocol::LoginReplyPayload{
            .reply_code = reply_code,
            .account_id = account_id,
        },
        client_version);

    return eq2::protocol::encode_application_packet(
        options_.login_reply_opcode, payload, options_.opcode_width);
  }

  [[nodiscard]] auto encode_world_list_reply_app(std::int16_t client_version) const
      -> std::vector<std::uint8_t> {
    auto worlds = std::vector<eq2::protocol::LoginWorldEntry>{};
    worlds.reserve(registered_worlds_.size());
    for (const auto& world : registered_worlds_) {
      worlds.push_back(eq2::protocol::LoginWorldEntry{
          .world_id = world.account_id,
          .display_name = world.display_name,
          .address = world.address,
          .locked = world.status == -2,
          .player_count = static_cast<std::uint16_t>(
              world.player_count < 0 ? 0 : std::min(world.player_count, 0xffff)),
          .development_server = world.is_development_server,
      });
    }

    const auto payload = eq2::protocol::encode_login_world_list_payload(
        eq2::protocol::LoginWorldListPayload{.worlds = std::move(worlds)},
        client_version);
    return eq2::protocol::encode_application_packet(
        options_.world_list_reply_opcode, payload, options_.opcode_width);
  }

  [[nodiscard]] auto encode_character_list_reply_app(std::uint32_t account_id,
                                                     std::int16_t client_version) const
      -> std::vector<std::uint8_t> {
    const auto payload = characters_ == nullptr
                             ? eq2::protocol::encode_empty_character_list_payload(account_id,
                                                                                  client_version)
                             : encode_character_list_payload(account_id, client_version);
    return eq2::protocol::encode_application_packet(
        options_.characters_reply_opcode, payload, options_.opcode_width);
  }

  static auto clamp_byte(std::int32_t value) -> std::uint8_t {
    return static_cast<std::uint8_t>(std::clamp(value, 0, 0xff));
  }

  static auto clamp_signed_byte(std::int32_t value) -> std::uint8_t {
    return static_cast<std::uint8_t>(std::clamp(value, -128, 127));
  }

  static auto scaled_signed_byte(double value) -> std::uint8_t {
    return clamp_signed_byte(static_cast<std::int32_t>(value * 100.0));
  }

  static void append_color(eq2::protocol::PacketWriter& writer,
                           const eq2::db::CharacterAppearanceRecord* color) {
    if (color == nullptr) {
      writer.append_u8(0);
      writer.append_u8(0);
      writer.append_u8(0);
      return;
    }

    writer.append_u8(clamp_byte(color->red));
    writer.append_u8(clamp_byte(color->green));
    writer.append_u8(clamp_byte(color->blue));
  }

  static void append_signed_triplet(eq2::protocol::PacketWriter& writer,
                                    const eq2::db::CharacterAppearanceRecord* value) {
    if (value == nullptr) {
      writer.append_u8(0);
      writer.append_u8(0);
      writer.append_u8(0);
      return;
    }

    writer.append_u8(clamp_signed_byte(value->red));
    writer.append_u8(clamp_signed_byte(value->green));
    writer.append_u8(clamp_signed_byte(value->blue));
  }

  static auto find_appearance(
      const std::vector<eq2::db::CharacterAppearanceRecord>& appearances,
      std::string_view type) -> const eq2::db::CharacterAppearanceRecord* {
    const auto iter = std::find_if(appearances.begin(), appearances.end(), [type](const auto& value) {
      return value.type == type;
    });
    return iter == appearances.end() ? nullptr : &*iter;
  }

  static auto find_equipment(
      const std::vector<eq2::db::CharacterEquipmentRecord>& equipment,
      std::int32_t slot) -> const eq2::db::CharacterEquipmentRecord* {
    const auto iter = std::find_if(equipment.begin(), equipment.end(), [slot](const auto& value) {
      return value.slot == slot;
    });
    return iter == equipment.end() ? nullptr : &*iter;
  }

  static void append_equipment(eq2::protocol::PacketWriter& writer,
                               const std::vector<eq2::db::CharacterEquipmentRecord>& equipment,
                               std::int32_t slots) {
    for (auto slot = 0; slot < slots; ++slot) {
      const auto* item = find_equipment(equipment, slot);
      eq2::protocol::append_i16_le(
          writer,
          static_cast<std::int16_t>(item == nullptr ? 0 : item->equip_type));
      writer.append_u8(clamp_byte(item == nullptr ? 0xff : item->red));
      writer.append_u8(clamp_byte(item == nullptr ? 0xff : item->green));
      writer.append_u8(clamp_byte(item == nullptr ? 0xff : item->blue));
      writer.append_u8(clamp_byte(item == nullptr ? 0xff : item->highlight_red));
      writer.append_u8(clamp_byte(item == nullptr ? 0xff : item->highlight_green));
      writer.append_u8(clamp_byte(item == nullptr ? 0xff : item->highlight_blue));
    }
  }

  static void append_legacy_appearance_group(
      eq2::protocol::PacketWriter& writer,
      std::int32_t type,
      const eq2::db::CharacterAppearanceRecord* color,
      const eq2::db::CharacterAppearanceRecord* highlight) {
    eq2::protocol::append_i16_le(writer, static_cast<std::int16_t>(type));
    append_signed_triplet(writer, color);
    append_signed_triplet(writer, highlight);
  }

  static auto positive_or_fallback(std::int32_t preferred,
                                   std::int32_t fallback) -> std::int32_t {
    return preferred > 0 ? preferred : fallback;
  }

  static auto character_select_packet_version(std::int16_t client_version) -> std::int32_t {
    if (client_version == 546 || client_version == 561) {
      return 11;
    }
    if (client_version >= 887) {
      return 6;
    }
    return 5;
  }

  static void append_legacy_character_profile(
      eq2::protocol::PacketWriter& writer,
      const eq2::db::CharacterListRecord& character,
      const std::vector<eq2::db::CharacterAppearanceRecord>& appearances,
      const std::vector<eq2::db::CharacterEquipmentRecord>& equipment,
      std::int16_t client_version) {
    eq2::protocol::append_i32_le(writer, character.character_id);
    eq2::protocol::append_i32_le(writer, character.server_id);
    eq2::protocol::append_eq2_16bit_string(writer, character.name);
    writer.append_u8(clamp_byte(character.race));
    writer.append_u8(clamp_byte(character.character_class));
    writer.append_u8(clamp_byte(character.gender));
    eq2::protocol::append_i32_le(writer, character.level);
    eq2::protocol::append_eq2_16bit_string(writer, character.zone_name.empty()
                                                     ? std::string_view{" "}
                                                     : std::string_view{character.zone_name});
    eq2::protocol::append_i32_le(writer, 0);
    eq2::protocol::append_i32_le(writer, 0);
    eq2::protocol::append_i32_le(writer, static_cast<std::int32_t>(character.created_date));
    eq2::protocol::append_i32_le(writer, static_cast<std::int32_t>(character.last_played));
    eq2::protocol::append_i32_le(writer, 57);
    eq2::protocol::append_i32_le(writer, 56);
    eq2::protocol::append_eq2_16bit_string(writer, " ");
    eq2::protocol::append_eq2_16bit_string(writer,
                                           character.zone_description.empty()
                                               ? std::string_view{" "}
                                               : std::string_view{character.zone_description});
    eq2::protocol::append_i32_le(writer, 0);
    writer.append_u8(clamp_byte(character_select_packet_version(client_version)));
    eq2::protocol::append_i16_le(writer, static_cast<std::int16_t>(character.model_type));

    append_signed_triplet(writer, find_appearance(appearances, "skin_color"));
    append_signed_triplet(writer, find_appearance(appearances, "eye_color"));
    append_equipment(writer, equipment, 23);
    append_legacy_appearance_group(writer,
                                   character.hair_type,
                                   find_appearance(appearances, "hair_type_color"),
                                   find_appearance(appearances, "hair_type_highlight_color"));
    append_legacy_appearance_group(writer,
                                   character.facial_hair_type,
                                   find_appearance(appearances, "hair_face_color"),
                                   find_appearance(appearances, "hair_face_highlight_color"));
    append_legacy_appearance_group(writer,
                                   character.chest_type,
                                   find_appearance(appearances, "shirt_color"),
                                   find_appearance(appearances, "unknown_chest_color"));
    append_legacy_appearance_group(writer,
                                   character.legs_type,
                                   find_appearance(appearances, "pants_color"),
                                   find_appearance(appearances, "unknown_legs_color"));
    append_signed_triplet(writer, find_appearance(appearances, "unknown9"));

    for (const auto type : {"eye_type", "ear_type", "eye_brow_type", "cheek_type",
                            "lip_type", "chin_type", "nose_type"}) {
      append_signed_triplet(writer, find_appearance(appearances, type));
    }

    writer.append_u8(scaled_signed_byte(character.body_size));
    writer.append_u8(scaled_signed_byte(character.body_age));
    eq2::protocol::append_i16_le(writer, 1377);
    writer.append_u8(57);
    writer.append_u8(0);
    writer.append_u8(0);
    append_signed_triplet(writer, find_appearance(appearances, "mount_color2"));
    append_signed_triplet(writer, find_appearance(appearances, "hair_color1"));
    append_signed_triplet(writer, find_appearance(appearances, "hair_color2"));
    append_signed_triplet(writer, find_appearance(appearances, "hair_color3"));

    for (auto index = std::size_t{0}; index < 10; ++index) {
      writer.append_u8(index < eq2::login::kLegacyCharacterSelectUnknown11.size()
                           ? eq2::login::kLegacyCharacterSelectUnknown11[index]
                           : 0);
    }

    eq2::protocol::append_i16_le(
        writer,
        static_cast<std::int16_t>(
            positive_or_fallback(character.soga_model_type, character.model_type)));
    append_color(writer, find_appearance(appearances, "soga_skin_color"));
    append_color(writer, find_appearance(appearances, "soga_eye_color"));
    writer.append_u8(0);
    writer.append_u8(0);
    writer.append_u8(0);
    for (const auto type : {"soga_eye_type", "soga_ear_type", "soga_eye_brow_type",
                            "soga_cheek_type", "soga_lip_type", "soga_chin_type",
                            "soga_nose_type"}) {
      append_signed_triplet(writer, find_appearance(appearances, type));
    }
    writer.append_u8(scaled_signed_byte(character.body_size));
    writer.append_u8(scaled_signed_byte(character.body_age));
    append_color(writer, find_appearance(appearances, "soga_hair_color1"));
    append_color(writer, find_appearance(appearances, "soga_hair_color2"));
    append_color(writer, find_appearance(appearances, "soga_hair_color3"));
    eq2::protocol::append_i16_le(
        writer,
        static_cast<std::int16_t>(
            positive_or_fallback(character.soga_hair_type, character.hair_type)));
    append_color(writer, find_appearance(appearances, "soga_hair_type_color"));
    append_color(writer, find_appearance(appearances, "soga_hair_type_highlight_color"));
    eq2::protocol::append_i16_le(
        writer,
        static_cast<std::int16_t>(positive_or_fallback(character.soga_facial_hair_type,
                                                       character.facial_hair_type)));
    append_color(writer, find_appearance(appearances, "soga_hair_face_color"));
    append_color(writer, find_appearance(appearances, "soga_hair_face_highlight_color"));
  }

  static void append_modern_character_profile(
      eq2::protocol::PacketWriter& writer,
      std::uint32_t account_id,
      const eq2::db::CharacterListRecord& character,
      const std::vector<eq2::db::CharacterAppearanceRecord>& appearances,
      const std::vector<eq2::db::CharacterEquipmentRecord>& equipment,
      std::int16_t client_version) {
    eq2::protocol::append_i32_le(writer, character_select_packet_version(client_version));
    eq2::protocol::append_i32_le(writer, character.character_id);
    eq2::protocol::append_i32_le(writer, character.server_id);
    eq2::protocol::append_eq2_16bit_string(writer, character.name);
    writer.append_u8(0);
    writer.append_u8(clamp_byte(character.race));
    writer.append_u8(clamp_byte(character.character_class));
    writer.append_u8(clamp_byte(character.gender));
    eq2::protocol::append_i32_le(writer, character.level);
    eq2::protocol::append_eq2_16bit_string(writer, character.zone_name.empty()
                                                     ? std::string_view{" "}
                                                     : std::string_view{character.zone_name});
    eq2::protocol::append_i32_le(writer, 0);
    eq2::protocol::append_i32_le(writer, 0);
    eq2::protocol::append_i32_le(writer, static_cast<std::int32_t>(character.created_date));
    eq2::protocol::append_i32_le(writer, static_cast<std::int32_t>(character.last_played));
    eq2::protocol::append_i32_le(writer, 57);
    eq2::protocol::append_i32_le(writer, 56);
    eq2::protocol::append_eq2_16bit_string(writer, " ");
    eq2::protocol::append_eq2_16bit_string(writer,
                                           character.zone_description.empty()
                                               ? std::string_view{" "}
                                               : std::string_view{character.zone_description});
    eq2::protocol::append_i32_le(writer, 0);
    eq2::protocol::append_eq2_16bit_string(writer, character.server_name);
    eq2::protocol::append_i32_le(writer, static_cast<std::int32_t>(account_id));
    writer.append_u8(0);
    writer.append_u8(1);
    eq2::protocol::append_i32_le(writer, 0);
    if (client_version >= 887) {
      writer.append_u8(0);
      eq2::protocol::append_i32_le(writer, 0);
    }
    writer.append_u8(15);
    eq2::protocol::append_i16_le(writer, static_cast<std::int16_t>(character.model_type));
    append_color(writer, find_appearance(appearances, "skin_color"));
    append_color(writer, find_appearance(appearances, "eye_color"));
    append_equipment(writer, equipment, 25);
    eq2::protocol::append_i16_le(writer, static_cast<std::int16_t>(character.hair_type));
    append_color(writer, find_appearance(appearances, "hair_type_color"));
    append_color(writer, find_appearance(appearances, "hair_type_highlight_color"));
    eq2::protocol::append_i16_le(writer, static_cast<std::int16_t>(character.facial_hair_type));
    append_color(writer, find_appearance(appearances, "hair_face_color"));
    append_color(writer, find_appearance(appearances, "hair_face_highlight_color"));
    if (client_version >= 887) {
      eq2::protocol::append_i16_le(writer, static_cast<std::int16_t>(character.wing_type));
      append_color(writer, find_appearance(appearances, "wing_color1"));
      append_color(writer, find_appearance(appearances, "wing_color2"));
    }
    eq2::protocol::append_i16_le(writer, static_cast<std::int16_t>(character.chest_type));
    append_color(writer, find_appearance(appearances, "shirt_color"));
    append_color(writer, find_appearance(appearances, "unknown_chest_color"));
    eq2::protocol::append_i16_le(writer, static_cast<std::int16_t>(character.legs_type));
    append_color(writer, find_appearance(appearances, "pants_color"));
    append_color(writer, find_appearance(appearances, "unknown_legs_color"));
    append_color(writer, find_appearance(appearances, "unknown9"));
    for (const auto type : {"eye_type", "ear_type", "eye_brow_type", "cheek_type",
                            "lip_type", "chin_type", "nose_type"}) {
      append_signed_triplet(writer, find_appearance(appearances, type));
    }
    writer.append_u8(scaled_signed_byte(character.body_size));
    for (auto index = 0; index < 9; ++index) {
      writer.append_u8(0);
    }
    append_color(writer, find_appearance(appearances, "hair_color1"));
    append_color(writer, find_appearance(appearances, "hair_color2"));
    for (const auto byte : eq2::login::kLegacyCharacterSelectUnknown11) {
      writer.append_u8(byte);
    }
    if (client_version >= 887) {
      eq2::protocol::append_i16_le(
          writer,
          static_cast<std::int16_t>(
              positive_or_fallback(character.soga_model_type, character.model_type)));
      append_color(writer, find_appearance(appearances, "soga_skin_color"));
      append_color(writer, find_appearance(appearances, "soga_eye_color"));
      writer.append_u8(0);
      writer.append_u8(0);
      writer.append_u8(0);
      for (const auto type : {"soga_eye_type", "soga_ear_type", "soga_eye_brow_type",
                              "soga_cheek_type", "soga_lip_type", "soga_chin_type",
                              "soga_nose_type"}) {
        append_signed_triplet(writer, find_appearance(appearances, type));
      }
      eq2::protocol::append_i16_le(writer, 212);
      append_color(writer, find_appearance(appearances, "soga_hair_color1"));
      append_color(writer, find_appearance(appearances, "soga_hair_color2"));
      writer.append_u8(0xff);
      writer.append_u8(0xff);
      writer.append_u8(0xff);
      eq2::protocol::append_i16_le(
          writer,
          static_cast<std::int16_t>(
              positive_or_fallback(character.soga_hair_type, character.hair_type)));
      append_color(writer, find_appearance(appearances, "soga_hair_type_color"));
      append_color(writer, find_appearance(appearances, "soga_hair_type_highlight_color"));
      eq2::protocol::append_i16_le(
          writer,
          static_cast<std::int16_t>(positive_or_fallback(character.soga_facial_hair_type,
                                                         character.facial_hair_type)));
      append_color(writer, find_appearance(appearances, "soga_hair_face_color"));
      append_color(writer, find_appearance(appearances, "soga_hair_face_highlight_color"));
    }
    for (auto index = 0; index < 7; ++index) {
      writer.append_u8(0);
    }
  }

  [[nodiscard]] auto encode_character_list_payload(std::uint32_t account_id,
                                                   std::int16_t client_version) const
      -> std::vector<std::uint8_t> {
    auto characters = std::vector<eq2::db::CharacterListRecord>{};
    for (const auto& character : characters_->load_character_list(static_cast<std::int32_t>(account_id))) {
      if (character.server_id != 0 && registered_world_is_available(character.server_id)) {
        characters.push_back(character);
      }
    }

    eq2::protocol::PacketWriter writer;
    writer.append_u8(static_cast<std::uint8_t>(std::min<std::size_t>(characters.size(), 0xffU)));
    for (auto index = std::size_t{0};
         index < characters.size() && index < static_cast<std::size_t>(0xffU);
         ++index) {
      const auto& character = characters[index];
      const auto appearances = characters_->load_character_appearance(character.login_character_id);
      const auto equipment = characters_->load_character_equipment(character.login_character_id);
      if (client_version <= 561) {
        append_legacy_character_profile(writer, character, appearances, equipment, client_version);
      } else {
        append_modern_character_profile(writer,
                                        account_id,
                                        character,
                                        appearances,
                                        equipment,
                                        client_version);
      }
    }

    eq2::protocol::append_i32_le(writer, static_cast<std::int32_t>(account_id));
    eq2::protocol::append_i32_le(writer, -1);
    eq2::protocol::append_i16_le(writer, 0);
    eq2::protocol::append_i32_le(writer, client_version <= 561 ? 7 : 10);
    writer.append_u8(0);

    if (client_version > 561) {
      for (auto index = 0; index < 3; ++index) {
        eq2::protocol::append_i32_le(writer, -1);
      }
      eq2::protocol::append_i32_le(writer, 0);
      writer.append_u8(0);
      writer.append_u8(0);
    }

    return std::move(writer).into_bytes();
  }

  LoginServerConfig config_;
  eq2::db::LoginAccountRepository& accounts_;
  eq2::db::WorldRegistrationRepository* worlds_ = nullptr;
  eq2::db::CharacterListRepository* characters_ = nullptr;
  eq2::db::ClientLogRepository* client_logs_ = nullptr;
  eq2::protocol::OpcodeVersionRanges supported_versions_;
  LiveLoginOptions options_;
  eq2::net::TcpSocketServer tcp_transport_;
  eq2::net::UdpSocketServer udp_transport_;

  mutable std::mutex mutex_;
  std::unordered_map<std::uint64_t, eq2::protocol::StreamPipeline> pipelines_;
  std::unordered_map<std::uint64_t, std::vector<std::uint8_t>> world_interserver_buffers_;
  std::unordered_map<std::uint64_t, std::int32_t> session_account_ids_;
  std::unordered_map<std::uint64_t, std::int32_t> session_world_ids_;
  std::unordered_map<std::uint64_t, std::int16_t> session_client_versions_;
  std::unordered_map<std::uint64_t, std::string> session_account_names_;
  std::unordered_map<std::uint64_t, std::string> session_remote_addresses_;
  std::unordered_map<std::int32_t, std::size_t> active_account_refcounts_;
  std::vector<LiveLoginEvent> events_;
  std::vector<LoginPacketOutcome> login_outcomes_;
  std::vector<WorldRegistrationResult> world_registration_results_;
  std::vector<RegisteredWorld> registered_worlds_;
  std::vector<PendingPlayRequest> pending_play_requests_;
  std::vector<PendingCreateRequest> pending_create_requests_;
  std::vector<std::vector<std::uint8_t>> outbound_;
  std::vector<std::vector<std::uint8_t>>* deferred_outbound_ = nullptr;
  std::optional<eq2::net::SessionId> deferred_outbound_session_;
  std::optional<eq2::net::TransportKind> deferred_outbound_transport_;
};

}  // namespace eq2::login

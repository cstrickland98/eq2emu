#pragma once

#include <cstdint>
#include <iterator>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <eq2/db/repositories.h>
#include <eq2/login/authentication.h>
#include <eq2/login/server.h>
#include <eq2/login/world_registration.h>
#include <eq2/protocol/application_packet.h>
#include <eq2/protocol/login_request.h>
#include <eq2/protocol/packet_buffer.h>
#include <eq2/protocol/protocol_packet.h>
#include <eq2/protocol/stream_pipeline.h>
#include <eq2/net/socket_transport.h>

namespace eq2::login {

inline constexpr std::uint16_t kLoginRequestAppOpcode = 0x1234;
inline constexpr std::uint16_t kLoginReplyAppOpcode = 0x1235;

enum class LiveLoginEventType {
  transport_accepted,
  session_requested,
  login_accepted,
  login_rejected,
  world_registered,
  world_rejected,
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

struct LiveLoginOptions {
  std::uint16_t requested_port = 0;
  std::uint16_t login_request_opcode = kLoginRequestAppOpcode;
  std::uint16_t login_reply_opcode = kLoginReplyAppOpcode;
  eq2::protocol::ApplicationOpcodeWidth opcode_width =
      eq2::protocol::ApplicationOpcodeWidth::two_bytes;
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
                         std::move(supported_versions),
                         options) {}

  LiveLoginService(const LiveLoginService&) = delete;
  auto operator=(const LiveLoginService&) -> LiveLoginService& = delete;

  auto start() -> bool {
    const auto port = options_.requested_port == 0 ? config_.port : options_.requested_port;
    return transport_.start(port);
  }

  void stop() {
    transport_.stop();
  }

  [[nodiscard]] auto port() const -> std::uint16_t {
    return transport_.port();
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

 private:
  LiveLoginService(LoginServerConfig config,
                   eq2::db::LoginAccountRepository& accounts,
                   eq2::db::WorldRegistrationRepository* worlds,
                   eq2::protocol::OpcodeVersionRanges supported_versions,
                   LiveLoginOptions options)
      : config_(std::move(config)),
        accounts_(accounts),
        worlds_(worlds),
        supported_versions_(std::move(supported_versions)),
        options_(options),
        transport_([this](eq2::net::SessionEvent event) { handle_transport_event(std::move(event)); }) {}
  void handle_transport_event(eq2::net::SessionEvent event) {
    std::lock_guard lock(mutex_);

    if (event.type == eq2::net::SessionEventType::accepted) {
      events_.push_back(LiveLoginEvent{
          .type = LiveLoginEventType::transport_accepted,
          .session = event.session,
      });
      return;
    }

    if (event.type == eq2::net::SessionEventType::disconnected) {
      pipelines_.erase(event.session.value);
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

    if (try_handle_world_registration(event)) {
      return;
    }

    auto& pipeline = pipelines_[event.session.value];
    auto result = pipeline.receive_datagram(event.bytes);
    outbound_.insert(outbound_.end(),
                     std::make_move_iterator(result.outbound.begin()),
                     std::make_move_iterator(result.outbound.end()));

    for (const auto& stream_event : result.events) {
      handle_stream_event(event.session, stream_event);
    }
  }

  auto try_handle_world_registration(const eq2::net::SessionEvent& event) -> bool {
    if (worlds_ == nullptr) {
      return false;
    }

    const auto packet = parse_world_registration_packet(event.bytes);
    if (!packet.has_value()) {
      return false;
    }

    auto result = register_world_server(*packet, false, *worlds_);
    world_registration_results_.push_back(result);
    events_.push_back(LiveLoginEvent{
        .type = result.status == WorldRegistrationStatus::accepted
                     ? LiveLoginEventType::world_registered
                     : LiveLoginEventType::world_rejected,
        .session = event.session,
        .reason = result.world.has_value() ? result.world->display_name : "",
    });
    return true;
  }

  void handle_stream_event(eq2::net::SessionId session,
                           const eq2::protocol::StreamEvent& stream_event) {
    switch (stream_event.type) {
      case eq2::protocol::StreamEventType::session_requested:
        events_.push_back(LiveLoginEvent{
            .type = LiveLoginEventType::session_requested,
            .session = session,
            .protocol_opcode = stream_event.protocol_opcode,
        });
        break;
      case eq2::protocol::StreamEventType::app_packet:
        handle_application_packet(session, stream_event);
        break;
      case eq2::protocol::StreamEventType::disconnected:
        pipelines_.erase(session.value);
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
          !active_account_ids_.insert(outcome.account->id).second;
    }

    login_outcomes_.push_back(outcome);
    outbound_.push_back(encode_login_reply(outcome));
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

  [[nodiscard]] auto encode_login_reply(const LoginPacketOutcome& outcome) const
      -> std::vector<std::uint8_t> {
    eq2::protocol::PacketWriter payload;
    payload.append_u8(static_cast<std::uint8_t>(outcome.reply_code));
    payload.append_u32_le(outcome.account.has_value() ? static_cast<std::uint32_t>(outcome.account->id) : 0);

    const auto app_packet = eq2::protocol::encode_application_packet(
        options_.login_reply_opcode, payload.bytes(), options_.opcode_width);
    return eq2::protocol::encode_protocol_packet(eq2::protocol::kOpPacket, app_packet);
  }

  LoginServerConfig config_;
  eq2::db::LoginAccountRepository& accounts_;
  eq2::db::WorldRegistrationRepository* worlds_ = nullptr;
  eq2::protocol::OpcodeVersionRanges supported_versions_;
  LiveLoginOptions options_;
  eq2::net::TcpSocketServer transport_;

  mutable std::mutex mutex_;
  std::unordered_map<std::uint64_t, eq2::protocol::StreamPipeline> pipelines_;
  std::unordered_set<std::int32_t> active_account_ids_;
  std::vector<LiveLoginEvent> events_;
  std::vector<LoginPacketOutcome> login_outcomes_;
  std::vector<WorldRegistrationResult> world_registration_results_;
  std::vector<std::vector<std::uint8_t>> outbound_;
};

}  // namespace eq2::login

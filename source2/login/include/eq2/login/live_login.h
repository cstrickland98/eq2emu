#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <eq2/db/repositories.h>
#include <eq2/login/authentication.h>
#include <eq2/login/server.h>
#include <eq2/login/world_registration.h>
#include <eq2/protocol/application_packet.h>
#include <eq2/protocol/login_response.h>
#include <eq2/protocol/login_request.h>
#include <eq2/protocol/packet_buffer.h>
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
  std::uint16_t world_list_reply_opcode = kWorldListReplyAppOpcode;
  std::uint16_t all_worlds_request_opcode = kAllWorldsRequestAppOpcode;
  std::uint16_t characters_request_opcode = kCharactersRequestAppOpcode;
  std::uint16_t characters_reply_opcode = kCharactersReplyAppOpcode;
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
        tcp_transport_([this](eq2::net::SessionEvent event) { handle_transport_event(std::move(event)); }),
        udp_transport_([this](eq2::net::SessionEvent event) { handle_transport_event(std::move(event)); }) {}
  void handle_transport_event(eq2::net::SessionEvent event) {
    std::lock_guard lock(mutex_);

    if (event.type == eq2::net::SessionEventType::accepted ||
        event.type == eq2::net::SessionEventType::connected) {
      events_.push_back(LiveLoginEvent{
          .type = LiveLoginEventType::transport_accepted,
          .session = event.session,
      });
      return;
    }

    if (event.type == eq2::net::SessionEventType::disconnected) {
      pipelines_.erase(session_key(event.session, event.transport));
      release_session_account(event.session, event.transport);
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
    if (result.status == WorldRegistrationStatus::accepted && result.world.has_value()) {
      upsert_registered_world(*result.world);
    }
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
      case eq2::protocol::StreamEventType::app_packet:
        handle_application_packet(session, transport, stream_event);
        break;
      case eq2::protocol::StreamEventType::disconnected:
        pipelines_.erase(session_key(session, transport));
        release_session_account(session, transport);
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
      session_client_versions_[session_key(session, transport)] = outcome.client_version;
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

  void send_or_capture(eq2::net::SessionId session,
                       eq2::net::TransportKind transport,
                       std::vector<std::uint8_t> packet) {
    if (transport == eq2::net::TransportKind::udp) {
      udp_transport_.send(session, packet);
    } else {
      tcp_transport_.send(session, packet);
    }
    outbound_.push_back(std::move(packet));
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

  [[nodiscard]] auto session_key(eq2::net::SessionId session,
                                 eq2::net::TransportKind transport) const -> std::uint64_t {
    return transport == eq2::net::TransportKind::udp ? (1ULL << 63U) | session.value
                                                     : session.value;
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
    const auto session_iter = session_account_ids_.find(session_key(session, transport));
    if (session_iter == session_account_ids_.end()) {
      return;
    }

    const auto account_id = session_iter->second;
    session_account_ids_.erase(session_iter);
    session_client_versions_.erase(session_key(session, transport));

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
    const auto payload =
        eq2::protocol::encode_empty_character_list_payload(account_id, client_version);
    return eq2::protocol::encode_application_packet(
        options_.characters_reply_opcode, payload, options_.opcode_width);
  }

  LoginServerConfig config_;
  eq2::db::LoginAccountRepository& accounts_;
  eq2::db::WorldRegistrationRepository* worlds_ = nullptr;
  eq2::protocol::OpcodeVersionRanges supported_versions_;
  LiveLoginOptions options_;
  eq2::net::TcpSocketServer tcp_transport_;
  eq2::net::UdpSocketServer udp_transport_;

  mutable std::mutex mutex_;
  std::unordered_map<std::uint64_t, eq2::protocol::StreamPipeline> pipelines_;
  std::unordered_map<std::uint64_t, std::int32_t> session_account_ids_;
  std::unordered_map<std::uint64_t, std::int16_t> session_client_versions_;
  std::unordered_map<std::int32_t, std::size_t> active_account_refcounts_;
  std::vector<LiveLoginEvent> events_;
  std::vector<LoginPacketOutcome> login_outcomes_;
  std::vector<WorldRegistrationResult> world_registration_results_;
  std::vector<RegisteredWorld> registered_worlds_;
  std::vector<std::vector<std::uint8_t>> outbound_;
};

}  // namespace eq2::login

#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <eq2/core/config.h>
#include <eq2/db/repositories.h>
#include <eq2/login/authentication.h>
#include <eq2/login/world_registration.h>
#include <eq2/net/tcp_server.h>
#include <eq2/protocol/login_request.h>
#include <eq2/protocol/opcode_version.h>

namespace eq2::login {

struct LoginServerConfig {
  std::string address = "0.0.0.0";
  std::uint16_t port = 9100;
  bool account_creation_allowed = false;
  eq2::net::BackpressurePolicy backpressure;
};

inline auto parse_u16_or(std::string_view value, std::uint16_t fallback) -> std::uint16_t {
  auto parsed = 0U;
  const auto* begin = value.data();
  const auto* end = value.data() + value.size();
  const auto [ptr, error] = std::from_chars(begin, end, parsed);
  if (error != std::errc{} || ptr != end || parsed > 0xffffU) {
    return fallback;
  }

  return static_cast<std::uint16_t>(parsed);
}

inline auto load_login_server_config(const eq2::core::ConfigProvider& config) -> LoginServerConfig {
  const auto address = config.get("login.address").value_or("0.0.0.0");
  const auto port = parse_u16_or(config.get("login.port").value_or("9100"), 9100);
  return LoginServerConfig{
      .address = address,
      .port = port,
      .account_creation_allowed = config.get("login.account_creation_allowed").value_or("false") == "true",
  };
}

enum class LoginPacketStatus {
  accepted,
  rejected,
  malformed,
};

struct LoginPacketOutcome {
  LoginPacketStatus status = LoginPacketStatus::malformed;
  LoginReplyCode reply_code = LoginReplyCode::invalid_username_or_password;
  std::optional<LoginAccount> account;
  std::int16_t client_version = 0;
  bool should_disconnect_current_session = true;
  bool should_disconnect_existing_session = false;
  bool should_send_world_list_after_login = false;
};

class LoginServer {
 public:
  LoginServer(LoginServerConfig config,
              eq2::db::LoginAccountRepository& accounts,
              eq2::db::WorldRegistrationRepository& worlds,
              eq2::protocol::OpcodeVersionRanges supported_versions)
      : config_(std::move(config)),
        accounts_(accounts),
        worlds_(worlds),
        supported_versions_(std::move(supported_versions)),
        transport_(
            eq2::net::TcpServerConfig{
                .listen = {.address = config_.address, .port = config_.port},
                .backpressure = config_.backpressure,
            },
            [this](eq2::net::SessionEvent event) { last_event_ = std::move(event); }) {}

  void start() {
    transport_.start();
  }

  void stop() {
    transport_.stop();
  }

  [[nodiscard]] auto endpoint() const -> eq2::net::TcpEndpoint {
    return transport_.endpoint();
  }

  auto accept_client() -> std::optional<eq2::net::SessionId> {
    return transport_.accept();
  }

  auto handle_login_request(eq2::net::SessionId session, std::span<const std::uint8_t> payload)
      -> LoginPacketOutcome {
    if (!transport_.receive(session, payload)) {
      return LoginPacketOutcome{};
    }

    const auto request = eq2::protocol::parse_legacy_login_request(payload);
    if (!request.has_value()) {
      return LoginPacketOutcome{
          .status = LoginPacketStatus::malformed,
      };
    }

    const auto authenticated = authenticate_login(
        LoginAuthenticationRequest{
            .credentials =
                LoginCredentials{
                    .username = request->username,
                    .password = request->password,
                },
            .client_version_is_supported = supported_versions_.contains_client_version(request->version),
            .account_creation_is_allowed = config_.account_creation_allowed,
            .account_already_has_session = false,
        },
        accounts_);

    return LoginPacketOutcome{
        .status = authenticated.reply_code == LoginReplyCode::accepted ? LoginPacketStatus::accepted
                                                                       : LoginPacketStatus::rejected,
        .reply_code = authenticated.reply_code,
        .account = authenticated.account,
        .client_version = request->version,
        .should_disconnect_current_session = authenticated.should_disconnect_current_session,
        .should_disconnect_existing_session = authenticated.should_disconnect_existing_session,
        .should_send_world_list_after_login = authenticated.should_send_world_list_after_login,
    };
  }

  auto handle_world_registration(std::span<const std::uint8_t> bytes, bool connection_is_authenticated)
      -> WorldRegistrationResult {
    const auto packet = parse_world_registration_packet(bytes);
    if (!packet.has_value()) {
      return WorldRegistrationResult{
          .status = WorldRegistrationStatus::rejected_bad_version,
      };
    }

    return register_world_server(*packet, connection_is_authenticated, worlds_);
  }

  [[nodiscard]] auto last_transport_event() const -> const std::optional<eq2::net::SessionEvent>& {
    return last_event_;
  }

 private:
  LoginServerConfig config_;
  eq2::db::LoginAccountRepository& accounts_;
  eq2::db::WorldRegistrationRepository& worlds_;
  eq2::protocol::OpcodeVersionRanges supported_versions_;
  eq2::net::TcpServer transport_;
  std::optional<eq2::net::SessionEvent> last_event_;
};

}  // namespace eq2::login

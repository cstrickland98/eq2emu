#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <eq2/core/config.h>
#include <eq2/db/repositories.h>
#include <eq2/net/tcp_server.h>
#include <eq2/protocol/interserver_packet.h>
#include <eq2/protocol/login_world.h>
#include <eq2/protocol/packet_header.h>

namespace eq2::world {

struct WorldServerConfig {
  std::string address = "0.0.0.0";
  std::uint16_t port = 9101;
  std::string advertised_address;
  std::string login_address = "127.0.0.1";
  std::uint16_t login_port = 9100;
  std::string world_name = "Source2 World";
  std::string world_account;
  std::string world_password;
  std::string protocol_version = "0.5.0";
  std::string server_version = "source2";
  std::uint32_t database_version = 0;
  eq2::protocol::ApplicationOpcodeWidth client_opcode_width =
      eq2::protocol::ApplicationOpcodeWidth::packed_u16;
  eq2::net::BackpressurePolicy backpressure;
};

inline auto parse_config_u16_or(std::string_view value, std::uint16_t fallback) -> std::uint16_t {
  auto parsed = 0U;
  const auto* begin = value.data();
  const auto* end = value.data() + value.size();
  const auto [ptr, error] = std::from_chars(begin, end, parsed);
  if (error != std::errc{} || ptr != end || parsed > 0xffffU) {
    return fallback;
  }

  return static_cast<std::uint16_t>(parsed);
}

inline auto parse_config_u32_or(std::string_view value, std::uint32_t fallback) -> std::uint32_t {
  auto parsed = std::uint32_t{0};
  const auto* begin = value.data();
  const auto* end = value.data() + value.size();
  const auto [ptr, error] = std::from_chars(begin, end, parsed);
  if (error != std::errc{} || ptr != end) {
    return fallback;
  }

  return parsed;
}

inline auto load_world_server_config(const eq2::core::ConfigProvider& config) -> WorldServerConfig {
  return WorldServerConfig{
      .address = config.get("world.address").value_or("0.0.0.0"),
      .port = parse_config_u16_or(config.get("world.port").value_or("9101"), 9101),
      .advertised_address = config.get("world.advertised_address").value_or(
          config.get("world.external_address").value_or("")),
      .login_address = config.get("login.remote_address").value_or(
          config.get("login.address").value_or("127.0.0.1")),
      .login_port = parse_config_u16_or(
          config.get("login.remote_port").value_or(config.get("login.port").value_or("9100")),
          9100),
      .world_name = config.get("world.name").value_or("Source2 World"),
      .world_account = config.get("world.account").value_or(""),
      .world_password = config.get("world.password").value_or(""),
      .protocol_version = config.get("world.protocol_version").value_or("0.5.0"),
      .server_version = config.get("world.server_version").value_or("source2"),
      .database_version = parse_config_u32_or(config.get("world.database_version").value_or("0"), 0),
      .client_opcode_width =
          eq2::protocol::parse_application_opcode_width(
              config.get("world.opcode_width").value_or(
                  config.get("world.application_opcode_width").value_or("packed")))
              .value_or(eq2::protocol::ApplicationOpcodeWidth::packed_u16),
  };
}

struct WorldClientSession {
  eq2::net::SessionId session;
  std::int32_t account_id = 0;
  std::int32_t access_key = 0;
  bool authenticated = false;
};

struct CharacterSummary {
  std::int32_t character_id = 0;
  std::int32_t server_id = 0;
  std::string name;
  std::int32_t level = 0;
  std::int32_t current_zone_id = 0;
};

struct CharacterSelectResult {
  bool accepted = false;
  CharacterSummary character;
};

struct ZoneHandoffRequest {
  std::int32_t account_id = 0;
  std::int32_t character_id = 0;
  std::int32_t zone_id = 0;
  std::int32_t access_key = 0;
};

struct ZoneHandoffResult {
  bool accepted = false;
  std::string reason;
};

class ZoneHandoff {
 public:
  virtual ~ZoneHandoff() = default;

  virtual auto request_zone_entry(const ZoneHandoffRequest& request) -> ZoneHandoffResult = 0;
};

inline auto make_world_registration_frame(const WorldServerConfig& config)
    -> std::optional<std::vector<std::uint8_t>> {
  const auto advertised_address = config.advertised_address.empty()
                                      ? config.address
                                      : config.advertised_address;
  const auto payload = eq2::protocol::encode_server_ls_info_payload(eq2::protocol::ServerLsInfo{
      .world_name = config.world_name,
      .address = advertised_address,
      .account = config.world_account,
      .password = config.world_password,
      .protocol_version = config.protocol_version,
      .server_version = config.server_version,
      .server_type = 0,
      .database_version = config.database_version,
  });

  return eq2::protocol::encode_interserver_packet(eq2::protocol::kServerOpLsInfo, payload);
}

class WorldServer {
 public:
  WorldServer(WorldServerConfig config,
              eq2::db::CharacterListRepository& characters,
              ZoneHandoff& zone_handoff)
      : config_(std::move(config)),
        characters_(characters),
        zone_handoff_(zone_handoff),
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

  auto registration_frame() const -> std::optional<std::vector<std::uint8_t>> {
    return make_world_registration_frame(config_);
  }

  auto accept_client() -> std::optional<eq2::net::SessionId> {
    return transport_.accept();
  }

  auto open_session(eq2::net::SessionId session, std::int32_t account_id, std::int32_t access_key)
      -> WorldClientSession {
    return WorldClientSession{
        .session = session,
        .account_id = account_id,
        .access_key = access_key,
        .authenticated = true,
    };
  }

  auto character_list(const WorldClientSession& session) -> std::vector<CharacterSummary> {
    auto summaries = std::vector<CharacterSummary>{};
    if (!session.authenticated) {
      return summaries;
    }

    for (const auto& row : characters_.load_character_list(session.account_id)) {
      summaries.push_back(CharacterSummary{
          .character_id = row.character_id,
          .server_id = row.server_id,
          .name = row.name,
          .level = row.level,
          .current_zone_id = row.current_zone_id,
      });
    }

    return summaries;
  }

  auto select_character(const WorldClientSession& session, std::int32_t character_id)
      -> CharacterSelectResult {
    for (const auto& character : character_list(session)) {
      if (character.character_id == character_id) {
        return CharacterSelectResult{
            .accepted = true,
            .character = character,
        };
      }
    }

    return {};
  }

  auto handoff_to_zone(const WorldClientSession& session, const CharacterSummary& character)
      -> ZoneHandoffResult {
    if (!session.authenticated) {
      return ZoneHandoffResult{
          .accepted = false,
          .reason = "world session is not authenticated",
      };
    }

    return zone_handoff_.request_zone_entry(ZoneHandoffRequest{
        .account_id = session.account_id,
        .character_id = character.character_id,
        .zone_id = character.current_zone_id,
        .access_key = session.access_key,
    });
  }

  [[nodiscard]] auto last_transport_event() const -> const std::optional<eq2::net::SessionEvent>& {
    return last_event_;
  }

 private:
  WorldServerConfig config_;
  eq2::db::CharacterListRepository& characters_;
  ZoneHandoff& zone_handoff_;
  eq2::net::TcpServer transport_;
  std::optional<eq2::net::SessionEvent> last_event_;
};

}  // namespace eq2::world

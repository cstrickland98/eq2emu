#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace eq2::login {

inline constexpr std::uint16_t kServerOpKeepAlive = 0x0001;
inline constexpr std::uint16_t kServerOpLsInfo = 0x1000;
inline constexpr std::size_t kServerLsInfoPayloadSize = 832;
inline constexpr std::string_view kLegacyInterserverProtocolVersion = "0.5.0";

enum class WorldServerType : std::uint8_t {
  world = 0,
  chat = 1,
  login = 2,
  mesh_login = 3,
  world_debug = 4,
};

enum class WorldRegistrationStatus {
  accepted,
  ignored,
  rejected_not_authenticated,
  rejected_bad_version,
  rejected_bad_password,
};

struct WorldRegistrationPacket {
  std::uint16_t opcode = 0;
  std::size_t payload_size = 0;
  std::string_view world_name;
  std::string_view address;
  std::string_view account;
  std::string_view password;
  std::string_view protocol_version;
  std::string_view server_version;
  WorldServerType server_type = WorldServerType::world;
};

struct RegisteredWorld {
  std::int32_t account_id = 0;
  std::string account;
  std::string display_name;
  std::string address;
  bool is_development_server = false;
};

struct WorldRegistrationResult {
  WorldRegistrationStatus status = WorldRegistrationStatus::ignored;
  std::optional<RegisteredWorld> world;
};

template <typename WorldAccountRepository>
auto register_world_server(const WorldRegistrationPacket& packet,
                           bool connection_is_authenticated,
                           WorldAccountRepository& accounts) -> WorldRegistrationResult {
  if (!connection_is_authenticated && packet.opcode != kServerOpLsInfo) {
    return WorldRegistrationResult{
        .status = WorldRegistrationStatus::rejected_not_authenticated,
    };
  }

  if (packet.opcode != kServerOpLsInfo) {
    return WorldRegistrationResult{
        .status = WorldRegistrationStatus::ignored,
    };
  }

  if (packet.payload_size != kServerLsInfoPayloadSize) {
    return WorldRegistrationResult{
        .status = WorldRegistrationStatus::rejected_bad_version,
    };
  }

  if (packet.protocol_version != kLegacyInterserverProtocolVersion ||
      !accounts.server_version_is_allowed(packet.server_version)) {
    return WorldRegistrationResult{
        .status = WorldRegistrationStatus::rejected_bad_version,
    };
  }

  if (packet.world_name.size() <= 3) {
    return WorldRegistrationResult{
        .status = WorldRegistrationStatus::rejected_bad_password,
    };
  }

  const auto account_id = accounts.check_server_account(packet.account, packet.password);
  if (account_id == 0 || accounts.account_is_disabled(packet.account) ||
      accounts.connection_ip_is_banned() || accounts.advertised_address_is_banned(packet.address)) {
    return WorldRegistrationResult{
        .status = WorldRegistrationStatus::rejected_bad_password,
    };
  }

  return WorldRegistrationResult{
      .status = WorldRegistrationStatus::accepted,
      .world =
          RegisteredWorld{
              .account_id = account_id,
              .account = std::string(packet.account),
              .display_name = accounts.display_name_for_account(account_id),
              .address = std::string(packet.address),
              .is_development_server = packet.server_type == WorldServerType::world_debug,
          },
  };
}

}  // namespace eq2::login

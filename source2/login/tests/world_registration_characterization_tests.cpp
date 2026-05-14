#include <eq2/login/world_registration.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

auto failures = 0;

void require(bool condition, std::string_view message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

template <typename T, typename U>
void require_eq(const T& actual, const U& expected, std::string_view message) {
  if (!(actual == expected)) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

class FakeWorldAccountRepository {
 public:
  void add_account(std::string account, std::string password, std::int32_t id, std::string display_name) {
    accounts_[account] = Account{
        .password = std::move(password),
        .id = id,
        .display_name = std::move(display_name),
    };
  }

  [[nodiscard]] auto server_version_is_allowed(std::string_view version) const -> bool {
    return version == "*" || version == "2026.05.14";
  }

  [[nodiscard]] auto check_server_account(std::string_view account, std::string_view password) const
      -> std::int32_t {
    const auto found = accounts_.find(std::string(account));
    if (found == accounts_.end() || found->second.password != password) {
      return 0;
    }

    return found->second.id;
  }

  [[nodiscard]] auto account_is_disabled(std::string_view account) const -> bool {
    return disabled_account_ == account;
  }

  [[nodiscard]] auto connection_ip_is_banned() const -> bool {
    return connection_ip_banned_;
  }

  [[nodiscard]] auto advertised_address_is_banned(std::string_view address) const -> bool {
    return banned_advertised_address_ == address;
  }

  [[nodiscard]] auto display_name_for_account(std::int32_t id) const -> std::string {
    for (const auto& [_, account] : accounts_) {
      if (account.id == id) {
        return account.display_name;
      }
    }

    return {};
  }

  void disable_account(std::string account) {
    disabled_account_ = std::move(account);
  }

  void ban_connection_ip() {
    connection_ip_banned_ = true;
  }

  void ban_advertised_address(std::string address) {
    banned_advertised_address_ = std::move(address);
  }

 private:
  struct Account {
    std::string password;
    std::int32_t id = 0;
    std::string display_name;
  };

  std::unordered_map<std::string, Account> accounts_;
  std::string disabled_account_;
  std::string banned_advertised_address_;
  bool connection_ip_banned_ = false;
};

auto valid_ls_info_packet() -> eq2::login::WorldRegistrationPacket {
  return eq2::login::WorldRegistrationPacket{
      .opcode = eq2::login::kServerOpLsInfo,
      .payload_size = eq2::login::kServerLsInfoPayloadSize,
      .world_name = "Public World",
      .address = "127.0.0.1",
      .account = "world-account",
      .password = "secret",
      .protocol_version = eq2::login::kLegacyInterserverProtocolVersion,
      .server_version = "2026.05.14",
      .server_type = eq2::login::WorldServerType::world,
  };
}

auto repository_with_valid_account() -> FakeWorldAccountRepository {
  FakeWorldAccountRepository accounts;
  accounts.add_account("world-account", "secret", 77, "Configured World Name");
  return accounts;
}

auto encode_ls_info_payload(const eq2::login::WorldRegistrationPacket& packet)
    -> std::vector<std::uint8_t> {
  eq2::protocol::PacketWriter writer;
  const auto append_fixed = [&writer](std::string_view value, std::size_t size) {
    std::vector<std::uint8_t> bytes(size, 0);
    const auto copy_size = std::min(value.size(), size);
    std::copy_n(reinterpret_cast<const std::uint8_t*>(value.data()), copy_size, bytes.data());
    writer.append_bytes(bytes);
  };

  append_fixed(packet.world_name, 201);
  append_fixed(packet.address, 250);
  append_fixed(packet.account, 31);
  append_fixed(packet.password, 256);
  append_fixed(packet.protocol_version, 25);
  append_fixed(packet.server_version, 64);
  writer.append_u8(static_cast<std::uint8_t>(packet.server_type));
  writer.append_u32_le(1234);

  return std::move(writer).into_bytes();
}

void unauthenticated_connections_must_send_ls_info_first() {
  auto accounts = repository_with_valid_account();

  auto packet = valid_ls_info_packet();
  packet.opcode = eq2::login::kServerOpKeepAlive;

  const auto result = eq2::login::register_world_server(packet, false, accounts);

  require_eq(result.status, eq2::login::WorldRegistrationStatus::rejected_not_authenticated,
             "unauthenticated non-LSInfo packet is rejected");
  require(!result.world.has_value(), "unauthenticated non-LSInfo packet does not register a world");
}

void valid_ls_info_registers_world() {
  auto accounts = repository_with_valid_account();

  const auto result = eq2::login::register_world_server(valid_ls_info_packet(), false, accounts);

  require_eq(result.status, eq2::login::WorldRegistrationStatus::accepted,
             "valid LSInfo packet registers the world");
  require(result.world.has_value(), "accepted LSInfo returns registered world data");
  require_eq(result.world->account_id, 77, "registered world uses account id from login DB");
  require_eq(result.world->display_name, std::string("Configured World Name"),
             "registered world display name comes from login DB account");
}

void invalid_protocol_or_server_version_is_bad_version() {
  auto accounts = repository_with_valid_account();

  auto bad_protocol = valid_ls_info_packet();
  bad_protocol.protocol_version = "0.4.9";
  const auto protocol_result = eq2::login::register_world_server(bad_protocol, false, accounts);

  require_eq(protocol_result.status, eq2::login::WorldRegistrationStatus::rejected_bad_version,
             "protocol mismatch is rejected as bad version");

  auto bad_server_version = valid_ls_info_packet();
  bad_server_version.server_version = "not-allowed";
  const auto version_result = eq2::login::register_world_server(bad_server_version, false, accounts);

  require_eq(version_result.status, eq2::login::WorldRegistrationStatus::rejected_bad_version,
             "disallowed world server version is rejected as bad version");
}

void invalid_account_or_ban_is_bad_password() {
  auto accounts = repository_with_valid_account();

  auto bad_password = valid_ls_info_packet();
  bad_password.password = "wrong";
  const auto password_result = eq2::login::register_world_server(bad_password, false, accounts);

  require_eq(password_result.status, eq2::login::WorldRegistrationStatus::rejected_bad_password,
             "invalid world account password is rejected as bad password");

  accounts.disable_account("world-account");
  const auto disabled_result = eq2::login::register_world_server(valid_ls_info_packet(), false, accounts);

  require_eq(disabled_result.status, eq2::login::WorldRegistrationStatus::rejected_bad_password,
             "disabled world account is rejected as bad password");
}

void debug_world_type_is_preserved() {
  auto accounts = repository_with_valid_account();

  auto packet = valid_ls_info_packet();
  packet.server_type = eq2::login::WorldServerType::world_debug;

  const auto result = eq2::login::register_world_server(packet, false, accounts);

  require_eq(result.status, eq2::login::WorldRegistrationStatus::accepted,
             "debug world type can register");
  require(result.world->is_development_server, "debug world type marks development server flag");
}

void raw_interserver_ls_info_packet_feeds_registration() {
  auto accounts = repository_with_valid_account();
  const auto payload = encode_ls_info_payload(valid_ls_info_packet());
  const auto bytes = eq2::protocol::encode_interserver_packet(eq2::login::kServerOpLsInfo, payload);
  require(bytes.has_value(), "raw LSInfo packet encodes");

  const auto packet = eq2::login::parse_world_registration_packet(*bytes);
  require(packet.has_value(), "raw LSInfo packet parses through protocol framing");

  const auto result = eq2::login::register_world_server(*packet, false, accounts);
  require_eq(result.status, eq2::login::WorldRegistrationStatus::accepted,
             "parsed raw LSInfo packet registers the world");
  require(result.world.has_value(), "parsed raw LSInfo carries registered world");
  require_eq(result.world->address, std::string("127.0.0.1"), "parsed raw LSInfo preserves address");
}

void raw_interserver_non_ls_info_keeps_authentication_gate() {
  auto accounts = repository_with_valid_account();
  const std::array<std::uint8_t, 1> payload{0};
  const auto bytes = eq2::protocol::encode_interserver_packet(eq2::login::kServerOpKeepAlive, payload);
  require(bytes.has_value(), "raw keepalive packet encodes");

  const auto packet = eq2::login::parse_world_registration_packet(*bytes);
  require(packet.has_value(), "raw non-LSInfo packet parses through protocol framing");

  const auto result = eq2::login::register_world_server(*packet, false, accounts);
  require_eq(result.status, eq2::login::WorldRegistrationStatus::rejected_not_authenticated,
             "parsed raw non-LSInfo packet still hits authentication gate");
}

}  // namespace

int main() {
  unauthenticated_connections_must_send_ls_info_first();
  valid_ls_info_registers_world();
  invalid_protocol_or_server_version_is_bad_version();
  invalid_account_or_ban_is_bad_password();
  debug_world_type_is_preserved();
  raw_interserver_ls_info_packet_feeds_registration();
  raw_interserver_non_ls_info_keeps_authentication_gate();

  if (failures != 0) {
    std::cerr << failures << " characterization assertion(s) failed\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

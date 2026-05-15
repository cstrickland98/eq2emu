#include <eq2/core/config.h>
#include <eq2/db/fake_database.h>
#include <eq2/login/server.h>
#include <eq2/protocol/interserver_packet.h>
#include <eq2/protocol/packet_buffer.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>
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

auto supported_login_versions() -> eq2::protocol::OpcodeVersionRanges {
  eq2::protocol::OpcodeVersionRanges ranges;
  ranges.add_range(546, 561);
  return ranges;
}

auto make_login_fixture(std::int16_t version) -> std::vector<std::uint8_t> {
  return eq2::protocol::encode_legacy_login_request_fixture(eq2::protocol::LoginRequest{
      .access_code = "station",
      .username = "tester",
      .password = "correct",
      .version = version,
  });
}

auto make_ls_info_frame(std::string_view account, std::string_view password)
    -> std::vector<std::uint8_t> {
  eq2::protocol::PacketWriter writer;
  const auto append_fixed = [&writer](std::string_view value, std::size_t size) {
    std::vector<std::uint8_t> bytes(size, 0);
    const auto copy_size = std::min(value.size(), size);
    std::copy_n(reinterpret_cast<const std::uint8_t*>(value.data()), copy_size, bytes.data());
    writer.append_bytes(bytes);
  };

  append_fixed("Public World", 201);
  append_fixed("127.0.0.1", 250);
  append_fixed(account, 31);
  append_fixed(password, 256);
  append_fixed("0.5.0", 25);
  append_fixed("2026.05.14", 64);
  writer.append_u8(0);
  writer.append_u32_le(1);

  return *eq2::protocol::encode_interserver_packet(eq2::protocol::kServerOpLsInfo, writer.bytes());
}

void login_server_config_loads_from_core_config() {
  eq2::core::MapConfig values;
  values.set("login.address", "127.0.0.1");
  values.set("login.port", "9101");
  values.set("login.account_creation_allowed", "true");

  const auto config = eq2::login::load_login_server_config(values);

  require_eq(config.address, std::string_view("127.0.0.1"), "login config reads address");
  require_eq(config.port, static_cast<std::uint16_t>(9101), "login config reads port");
  require(config.account_creation_allowed, "login config reads account creation flag");
}

void client_login_reaches_legacy_success_outcome_through_source2_boundaries() {
  eq2::db::FakeLoginAccountRepository accounts;
  accounts.add_account(42, "tester", "correct");
  eq2::db::FakeWorldRegistrationRepository worlds;
  eq2::login::LoginServer server(
      eq2::login::LoginServerConfig{.address = "127.0.0.1", .port = 9100},
      accounts,
      worlds,
      supported_login_versions());

  server.start();
  const auto session = server.accept_client();
  require(session.has_value(), "login server accepts a source2 net session");

  const auto outcome = server.handle_login_request(*session, make_login_fixture(546));

  require_eq(outcome.status, eq2::login::LoginPacketStatus::accepted,
             "valid login reaches accepted outcome");
  require_eq(outcome.reply_code, eq2::login::LoginReplyCode::accepted,
             "valid login uses legacy accepted reply code");
  require(outcome.account.has_value(), "accepted login carries account");
  require_eq(outcome.account->id, 42, "accepted login uses DB account id");
  require_eq(outcome.client_version, static_cast<std::int16_t>(546),
             "accepted login preserves parsed client version");
  require(outcome.should_send_world_list_after_login, "accepted login schedules world list");
  require(server.last_transport_event().has_value(), "login server records transport event");
  require_eq(server.last_transport_event()->type, eq2::net::SessionEventType::received,
             "login bytes arrive through net receive event");
}

void unsupported_client_version_reaches_legacy_bad_version_outcome() {
  eq2::db::FakeLoginAccountRepository accounts;
  accounts.add_account(42, "tester", "correct");
  eq2::db::FakeWorldRegistrationRepository worlds;
  eq2::login::LoginServer server(
      eq2::login::LoginServerConfig{},
      accounts,
      worlds,
      supported_login_versions());

  server.start();
  const auto session = server.accept_client();
  require(session.has_value(), "login server accepts session for bad-version test");

  const auto outcome = server.handle_login_request(*session, make_login_fixture(1208));

  require_eq(outcome.status, eq2::login::LoginPacketStatus::rejected,
             "unsupported version is rejected");
  require_eq(outcome.reply_code, eq2::login::LoginReplyCode::bad_client_version,
             "unsupported version uses legacy bad-version reply code");
  require(!outcome.account.has_value(), "bad version does not return an account");
}

void world_registration_uses_protocol_frame_and_db_repository() {
  eq2::db::FakeLoginAccountRepository accounts;
  eq2::db::FakeWorldRegistrationRepository worlds;
  worlds.allowed_server_versions.insert("2026.05.14");
  worlds.accounts["world-account"] = eq2::db::WorldAccountRecord{
      .id = 77,
      .account = "world-account",
      .display_name = "Public World",
      .password = "secret",
  };
  eq2::login::LoginServer server(
      eq2::login::LoginServerConfig{},
      accounts,
      worlds,
      supported_login_versions());

  const auto frame = make_ls_info_frame("world-account", "secret");
  const auto result = server.handle_world_registration(frame, false);

  require_eq(result.status, eq2::login::WorldRegistrationStatus::accepted,
             "LSInfo world registration is accepted through source2 db repository");
  require(result.world.has_value(), "accepted world registration returns world record");
  require_eq(result.world->account_id, 77, "accepted world registration carries account id");
  require_eq(result.world->display_name, std::string_view("Public World"),
             "accepted world registration carries display name");
}

}  // namespace

int main() {
  login_server_config_loads_from_core_config();
  client_login_reaches_legacy_success_outcome_through_source2_boundaries();
  unsupported_client_version_reaches_legacy_bad_version_outcome();
  world_registration_uses_protocol_frame_and_db_repository();

  if (failures != 0) {
    std::cerr << failures << " login server assertion(s) failed\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

#include <eq2/core/config.h>
#include <eq2/db/fake_database.h>
#include <eq2/db/query.h>
#include <eq2/db/sql_repositories.h>
#include <eq2/login/live_login.h>
#include <eq2/login/server.h>
#include <eq2/protocol/application_packet.h>
#include <eq2/protocol/interserver_packet.h>
#include <eq2/protocol/packet_buffer.h>
#include <eq2/protocol/protocol_packet.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
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

class ScriptedLoginConnection final : public eq2::db::QueryConnection {
 public:
  auto execute(const eq2::db::QueryRequest& request)
      -> eq2::core::Result<eq2::db::QueryResult> override {
    requests.push_back(request);

    if (request.sql.find("select id, name from account") != std::string::npos &&
        request.parameters.size() >= 2) {
      const auto& username = request.parameters[0];
      const auto& password = request.parameters[1];
      if (username == "tester" && password == "correct") {
        return row(42, "tester");
      }
      return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{});
    }

    if (request.sql.find("select id from account") != std::string::npos &&
        !request.parameters.empty()) {
      if (existing_accounts_contains(request.parameters.front())) {
        return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{
            .rows = {eq2::db::QueryRow{.columns = {{"id", "42"}}}},
        });
      }
      return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{});
    }

    if (request.sql.find("insert into account") != std::string::npos &&
        !request.parameters.empty()) {
      created_accounts.push_back(request.parameters.front());
      return row(88, request.parameters.front());
    }

    return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{});
  }

  std::vector<eq2::db::QueryRequest> requests;
  std::vector<std::string> created_accounts;

 private:
  static auto row(std::int32_t id, std::string_view name)
      -> eq2::core::Result<eq2::db::QueryResult> {
    return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{
        .rows =
            {
                eq2::db::QueryRow{
                    .columns = {{"id", std::to_string(id)}, {"name", std::string(name)}}},
            },
        .affected_rows = 1,
    });
  }

  static auto existing_accounts_contains(std::string_view username) -> bool {
    return username == "tester";
  }
};

auto make_login_fixture(std::int16_t version) -> std::vector<std::uint8_t> {
  return eq2::protocol::encode_legacy_login_request_fixture(eq2::protocol::LoginRequest{
      .access_code = "station",
      .username = "tester",
      .password = "correct",
      .version = version,
  });
}

auto make_live_login_frame(std::string_view username,
                           std::string_view password,
                           std::int16_t version) -> std::vector<std::uint8_t> {
  const auto login_payload = eq2::protocol::encode_legacy_login_request_fixture(
      eq2::protocol::LoginRequest{
          .access_code = "station",
          .username = std::string(username),
          .password = std::string(password),
          .version = version,
      });
  const auto app_packet = eq2::protocol::encode_application_packet(
      eq2::login::kLoginRequestAppOpcode, login_payload);
  return eq2::protocol::encode_protocol_packet(eq2::protocol::kOpPacket, app_packet);
}

auto wait_for_login_count(const eq2::login::LiveLoginService& service, std::size_t count) -> bool {
  for (auto attempt = 0; attempt < 80; ++attempt) {
    if (service.login_outcomes().size() >= count) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  return false;
}

auto wait_for_event(const eq2::login::LiveLoginService& service,
                    eq2::login::LiveLoginEventType type) -> bool {
  for (auto attempt = 0; attempt < 80; ++attempt) {
    const auto events = service.events();
    if (std::any_of(events.begin(), events.end(), [type](const auto& event) {
          return event.type == type;
        })) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  return false;
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

void live_login_slice_uses_real_tcp_protocol_pipeline_and_sql_repository() {
  auto connection = std::make_shared<ScriptedLoginConnection>();
  eq2::db::SqlLoginAccountRepository accounts(connection);
  eq2::login::LiveLoginService service(
      eq2::login::LoginServerConfig{.address = "127.0.0.1", .port = 0},
      accounts,
      supported_login_versions());

  require(service.start(), "live login service starts real loopback TCP transport");
  require(service.port() != 0, "live login service binds an ephemeral test port");

  require(eq2::net::send_tcp_loopback(service.port(), make_live_login_frame("tester", "correct", 546)),
          "live login smoke harness sends valid login over real TCP");
  require(wait_for_login_count(service, 1), "live login service handles valid login frame");
  auto outcomes = service.login_outcomes();
  require_eq(outcomes[0].status, eq2::login::LoginPacketStatus::accepted,
             "live login accepts valid SQL-backed credentials");
  require_eq(outcomes[0].reply_code, eq2::login::LoginReplyCode::accepted,
             "live login preserves accepted reply code");
  require(outcomes[0].account.has_value(), "live login returns SQL-backed account");
  require_eq(outcomes[0].account->id, 42, "live login reads account id from SQL repository");
  require(!connection->requests.empty(), "live login exercises SQL account repository");
  require_eq(connection->requests.front().parameters.front(), std::string_view("tester"),
             "live login passes username through SQL repository parameters");
  require(!service.outbound_packets().empty(), "live login captures protocol reply boundary");

  require(eq2::net::send_tcp_loopback(service.port(), make_live_login_frame("tester", "correct", 546)),
          "live login smoke harness sends duplicate login over real TCP");
  require(wait_for_login_count(service, 2), "live login service handles duplicate login frame");
  outcomes = service.login_outcomes();
  require(outcomes[1].should_disconnect_existing_session,
          "live login flags duplicate session behavior");

  require(eq2::net::send_tcp_loopback(service.port(), make_live_login_frame("tester", "wrong", 546)),
          "live login smoke harness sends invalid password over real TCP");
  require(wait_for_login_count(service, 3), "live login service handles invalid password frame");
  outcomes = service.login_outcomes();
  require_eq(outcomes[2].reply_code, eq2::login::LoginReplyCode::invalid_username_or_password,
             "live login preserves invalid password reply code");

  require(eq2::net::send_tcp_loopback(service.port(), make_live_login_frame("tester", "correct", 1208)),
          "live login smoke harness sends bad client version over real TCP");
  require(wait_for_login_count(service, 4), "live login service handles bad client version frame");
  outcomes = service.login_outcomes();
  require_eq(outcomes[3].reply_code, eq2::login::LoginReplyCode::bad_client_version,
             "live login preserves bad client version reply code");

  const std::vector<std::uint8_t> malformed{0x00};
  require(eq2::net::send_tcp_loopback(service.port(), malformed),
          "live login smoke harness sends malformed protocol bytes");
  require(wait_for_event(service, eq2::login::LiveLoginEventType::malformed),
          "live login records malformed protocol packet");

  const std::vector<std::uint8_t> empty_payload;
  const auto disconnect = eq2::protocol::encode_protocol_packet(
      eq2::protocol::kOpSessionDisconnect, empty_payload);
  require(eq2::net::send_tcp_loopback(service.port(), disconnect),
          "live login smoke harness sends protocol disconnect");
  require(wait_for_event(service, eq2::login::LiveLoginEventType::disconnected),
          "live login records graceful disconnect");

  service.stop();
}

void live_login_slice_can_create_accounts_when_policy_allows_it() {
  auto connection = std::make_shared<ScriptedLoginConnection>();
  eq2::db::SqlLoginAccountRepository accounts(connection);
  eq2::login::LiveLoginService service(
      eq2::login::LoginServerConfig{
          .address = "127.0.0.1",
          .port = 0,
          .account_creation_allowed = true,
      },
      accounts,
      supported_login_versions());

  require(service.start(), "live login creation test starts real loopback TCP transport");
  require(eq2::net::send_tcp_loopback(service.port(), make_live_login_frame("newbie", "newpass", 546)),
          "live login smoke harness sends new account request over real TCP");
  require(wait_for_login_count(service, 1), "live login service handles account creation frame");

  const auto outcomes = service.login_outcomes();
  require_eq(outcomes.front().reply_code, eq2::login::LoginReplyCode::accepted,
             "live login accepts account creation when configured");
  require(outcomes.front().account.has_value(), "live login returns created account");
  require_eq(outcomes.front().account->id, 88, "live login maps created account id");
  require_eq(connection->created_accounts.front(), std::string_view("newbie"),
             "live login creates account through SQL repository boundary");

  service.stop();
}

}  // namespace

int main() {
  login_server_config_loads_from_core_config();
  client_login_reaches_legacy_success_outcome_through_source2_boundaries();
  unsupported_client_version_reaches_legacy_bad_version_outcome();
  world_registration_uses_protocol_frame_and_db_repository();
  live_login_slice_uses_real_tcp_protocol_pipeline_and_sql_repository();
  live_login_slice_can_create_accounts_when_policy_allows_it();

  if (failures != 0) {
    std::cerr << failures << " login server assertion(s) failed\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

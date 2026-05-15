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
#include <eq2/protocol/session.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
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

    if (request.sql.find("select version from login_versions") != std::string::npos) {
      return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{
          .rows = {eq2::db::QueryRow{.columns = {{"version", "*"}}}},
      });
    }

    if (request.sql.find("select id from login_worldservers") != std::string::npos &&
        request.parameters.size() >= 2 && request.parameters[0] == "world-account" &&
        request.parameters[1] == "secret") {
      return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{
          .rows = {eq2::db::QueryRow{.columns = {{"id", "77"}}}},
      });
    }

    if (request.sql.find("select disabled from login_worldservers") != std::string::npos &&
        !request.parameters.empty() && request.parameters[0] == "world-account") {
      return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{
          .rows = {eq2::db::QueryRow{.columns = {{"disabled", "0"}}}},
      });
    }

    if (request.sql.find("select ip from login_bannedips") != std::string::npos) {
      return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{});
    }

    if (request.sql.find("select name from login_worldservers") != std::string::npos &&
        !request.parameters.empty() && request.parameters[0] == "77") {
      return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{
          .rows = {eq2::db::QueryRow{.columns = {{"name", "Public World"}}}},
      });
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
                           std::int16_t version,
                           std::uint16_t login_request_opcode = eq2::login::kLoginRequestAppOpcode,
                           eq2::protocol::ApplicationOpcodeWidth opcode_width =
                               eq2::protocol::ApplicationOpcodeWidth::two_bytes)
    -> std::vector<std::uint8_t> {
  const auto login_payload = eq2::protocol::encode_legacy_login_request_fixture(
      eq2::protocol::LoginRequest{
          .access_code = "station",
          .username = std::string(username),
          .password = std::string(password),
          .version = version,
      });
  const auto app_packet = eq2::protocol::encode_application_packet(
      login_request_opcode, login_payload, opcode_width);
  return eq2::protocol::encode_protocol_packet(eq2::protocol::kOpPacket, app_packet);
}

auto make_sequenced_live_login_frame(std::string_view username,
                                     std::string_view password,
                                     std::int16_t version,
                                     std::uint16_t sequence,
                                     std::uint16_t login_request_opcode =
                                         eq2::login::kLoginRequestAppOpcode,
                                     eq2::protocol::ApplicationOpcodeWidth opcode_width =
                                         eq2::protocol::ApplicationOpcodeWidth::two_bytes)
    -> std::vector<std::uint8_t> {
  const auto login_payload = eq2::protocol::encode_legacy_login_request_fixture(
      eq2::protocol::LoginRequest{
          .access_code = "station",
          .username = std::string(username),
          .password = std::string(password),
          .version = version,
      });
  const auto app_packet = eq2::protocol::encode_application_packet(
      login_request_opcode, login_payload, opcode_width);
  eq2::protocol::PacketWriter writer;
  writer.append_u16_be(sequence);
  writer.append_bytes(app_packet);
  return eq2::protocol::encode_protocol_packet(eq2::protocol::kOpPacket, writer.bytes());
}

auto make_sequenced_all_worlds_request_frame(
    std::uint16_t sequence,
    std::uint16_t all_worlds_request_opcode = eq2::login::kAllWorldsRequestAppOpcode,
    eq2::protocol::ApplicationOpcodeWidth opcode_width =
        eq2::protocol::ApplicationOpcodeWidth::two_bytes) -> std::vector<std::uint8_t> {
  const auto app_packet = eq2::protocol::encode_application_packet(
      all_worlds_request_opcode, std::span<const std::uint8_t>{}, opcode_width);
  eq2::protocol::PacketWriter writer;
  writer.append_u16_be(sequence);
  writer.append_bytes(app_packet);
  return eq2::protocol::encode_protocol_packet(eq2::protocol::kOpPacket, writer.bytes());
}

auto login_over_session_handshake(std::uint16_t port,
                                  std::string_view username,
                                  std::string_view password,
                                  std::int16_t version,
                                  std::uint16_t login_request_opcode =
                                      eq2::login::kLoginRequestAppOpcode,
                                  eq2::protocol::ApplicationOpcodeWidth opcode_width =
                                      eq2::protocol::ApplicationOpcodeWidth::two_bytes)
    -> std::optional<std::vector<std::uint8_t>> {
  eq2::net::TcpSocketClient client;
  if (!client.connect_to(eq2::net::SocketEndpoint{.address = "127.0.0.1", .port = port})) {
    return std::nullopt;
  }

  const auto session_request = eq2::protocol::encode_protocol_packet(
      eq2::protocol::kOpSessionRequest,
      eq2::protocol::encode_session_request(eq2::protocol::SessionRequest{
          .unknown_a = 0,
          .session = 0x10203040,
          .max_length = 512,
      }));
  if (!client.send(session_request)) {
    return std::nullopt;
  }

  const auto session_response = client.receive();
  if (!session_response.has_value()) {
    return std::nullopt;
  }

  const auto protocol = eq2::protocol::decode_protocol_packet(*session_response);
  if (!protocol.has_value() || protocol->opcode != eq2::protocol::kOpSessionResponse) {
    return std::nullopt;
  }

  if (!client.send(make_sequenced_live_login_frame(
          username, password, version, 0, login_request_opcode, opcode_width))) {
    return std::nullopt;
  }

  for (auto attempt = 0; attempt < 4; ++attempt) {
    auto response = client.receive();
    if (!response.has_value()) {
      return std::nullopt;
    }

    const auto response_protocol = eq2::protocol::decode_protocol_packet(*response);
    if (response_protocol.has_value() && response_protocol->opcode == eq2::protocol::kOpAck) {
      continue;
    }
    return response;
  }

  return std::nullopt;
}

auto login_over_udp_session_handshake(std::uint16_t port,
                                      std::string_view username,
                                      std::string_view password,
                                      std::int16_t version,
                                      std::uint16_t login_request_opcode =
                                          eq2::login::kLoginRequestAppOpcode,
                                      eq2::protocol::ApplicationOpcodeWidth opcode_width =
                                          eq2::protocol::ApplicationOpcodeWidth::two_bytes)
    -> std::optional<std::vector<std::uint8_t>> {
  eq2::net::UdpSocketClient client;
  if (!client.connect_to(eq2::net::SocketEndpoint{.address = "127.0.0.1", .port = port})) {
    return std::nullopt;
  }

  const auto session_request = eq2::protocol::encode_protocol_packet(
      eq2::protocol::kOpSessionRequest,
      eq2::protocol::encode_session_request(eq2::protocol::SessionRequest{
          .unknown_a = 0,
          .session = 0x10203040,
          .max_length = 512,
      }));
  if (!client.send(session_request)) {
    return std::nullopt;
  }

  const auto session_response = client.receive();
  if (!session_response.has_value()) {
    return std::nullopt;
  }

  const auto protocol = eq2::protocol::decode_protocol_packet(*session_response);
  if (!protocol.has_value() || protocol->opcode != eq2::protocol::kOpSessionResponse) {
    return std::nullopt;
  }

  if (!client.send(make_sequenced_live_login_frame(
          username, password, version, 0, login_request_opcode, opcode_width))) {
    return std::nullopt;
  }

  for (auto attempt = 0; attempt < 4; ++attempt) {
    auto response = client.receive();
    if (!response.has_value()) {
      return std::nullopt;
    }

    const auto response_protocol = eq2::protocol::decode_protocol_packet(*response);
    if (response_protocol.has_value() && response_protocol->opcode == eq2::protocol::kOpAck) {
      continue;
    }
    return response;
  }

  return std::nullopt;
}

auto open_login_session(eq2::net::TcpSocketClient& client, std::uint16_t port) -> bool {
  if (!client.connect_to(eq2::net::SocketEndpoint{.address = "127.0.0.1", .port = port})) {
    return false;
  }

  const auto session_request = eq2::protocol::encode_protocol_packet(
      eq2::protocol::kOpSessionRequest,
      eq2::protocol::encode_session_request(eq2::protocol::SessionRequest{
          .unknown_a = 0,
          .session = 0x10203040,
          .max_length = 512,
      }));
  if (!client.send(session_request)) {
    return false;
  }

  const auto session_response = client.receive();
  if (!session_response.has_value()) {
    return false;
  }

  const auto protocol = eq2::protocol::decode_protocol_packet(*session_response);
  if (!protocol.has_value() || protocol->opcode != eq2::protocol::kOpSessionResponse) {
    return false;
  }

  return true;
}

auto decode_reply_account_id(std::span<const std::uint8_t> login_response)
    -> std::optional<std::uint32_t>;

auto decode_reply_account_id(std::span<const std::uint8_t> login_response,
                             std::uint16_t login_reply_opcode,
                             eq2::protocol::ApplicationOpcodeWidth opcode_width)
    -> std::optional<std::uint32_t> {
  const auto reply = eq2::protocol::decode_login_reply_protocol_frame(
      login_response, login_reply_opcode, opcode_width);
  if (!reply.has_value() ||
      reply->reply_code != static_cast<std::uint8_t>(eq2::login::LoginReplyCode::accepted)) {
    return std::nullopt;
  }

  return reply->account_id;
}

auto decode_reply_account_id(std::span<const std::uint8_t> login_response)
    -> std::optional<std::uint32_t> {
  return decode_reply_account_id(login_response,
                                 eq2::login::kLoginReplyAppOpcode,
                                 eq2::protocol::ApplicationOpcodeWidth::two_bytes);
}

auto decode_world_list_response(std::span<const std::uint8_t> login_response)
    -> std::optional<eq2::protocol::LoginWorldListPayload> {
  const auto protocol = eq2::protocol::decode_protocol_packet(login_response);
  if (!protocol.has_value() || protocol->opcode != eq2::protocol::kOpPacket) {
    return std::nullopt;
  }

  const auto decode_app =
      [](std::span<const std::uint8_t> payload)
      -> std::optional<eq2::protocol::LoginWorldListPayload> {
    const auto app = eq2::protocol::decode_application_packet(payload);
    if (!app.has_value() || app->opcode != eq2::login::kWorldListReplyAppOpcode) {
      return std::nullopt;
    }

    return eq2::protocol::decode_login_world_list_payload(app->payload);
  };

  if (protocol->payload.size() >= 2) {
    if (auto sequenced = decode_app(protocol->payload.subspan(2))) {
      return sequenced;
    }
  }

  return decode_app(protocol->payload);
}

auto decode_character_list_account_id(std::span<const std::uint8_t> login_response)
    -> std::optional<std::uint32_t> {
  const auto protocol = eq2::protocol::decode_protocol_packet(login_response);
  if (!protocol.has_value() || protocol->opcode != eq2::protocol::kOpPacket) {
    return std::nullopt;
  }

  const auto decode_app = [](std::span<const std::uint8_t> payload) -> std::optional<std::uint32_t> {
    const auto app = eq2::protocol::decode_application_packet(payload);
    if (!app.has_value() || app->opcode != eq2::login::kCharactersReplyAppOpcode ||
        app->payload.size() < 5) {
      return std::nullopt;
    }

    eq2::protocol::PacketReader reader(app->payload);
    const auto character_count = reader.read_u8();
    const auto account_id = reader.read_u32_le();
    if (!character_count.has_value() || !account_id.has_value() || *character_count != 0) {
      return std::nullopt;
    }
    return account_id;
  };

  if (protocol->payload.size() >= 2) {
    if (auto sequenced = decode_app(protocol->payload.subspan(2))) {
      return sequenced;
    }
  }

  return decode_app(protocol->payload);
}

auto decode_login_reply_code(std::span<const std::uint8_t> login_response)
    -> std::optional<std::uint8_t> {
  const auto reply = eq2::protocol::decode_login_reply_protocol_frame(
      login_response, eq2::login::kLoginReplyAppOpcode);
  if (!reply.has_value()) {
    return std::nullopt;
  }
  return reply->reply_code;
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

auto wait_for_event_count(const eq2::login::LiveLoginService& service,
                          eq2::login::LiveLoginEventType type,
                          std::size_t count) -> bool {
  for (auto attempt = 0; attempt < 80; ++attempt) {
    const auto events = service.events();
    const auto observed = static_cast<std::size_t>(
        std::count_if(events.begin(), events.end(), [type](const auto& event) {
          return event.type == type;
        }));
    if (observed >= count) {
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

  const auto login_response =
      login_over_session_handshake(service.port(), "tester", "correct", 546);
  require(login_response.has_value(),
          "live login writes valid login reply after real TCP session handshake");
  if (login_response.has_value()) {
    const auto protocol = eq2::protocol::decode_protocol_packet(*login_response);
    require(protocol.has_value(), "live login TCP reply decodes as a protocol packet");
    require(protocol.has_value() && protocol->opcode == eq2::protocol::kOpPacket,
            "live login TCP reply uses application protocol opcode");
    require_eq(decode_reply_account_id(*login_response).value_or(0), std::uint32_t{42},
               "live login TCP reply carries SQL-backed account id");
  }
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
          "live login smoke harness sends second valid login over real TCP");
  require(wait_for_login_count(service, 2), "live login service handles second valid login frame");
  outcomes = service.login_outcomes();
  require_eq(outcomes[1].reply_code, eq2::login::LoginReplyCode::accepted,
             "live login preserves accepted reply code for second valid login");

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

void live_login_slice_accepts_udp_client_login() {
  auto connection = std::make_shared<ScriptedLoginConnection>();
  eq2::db::SqlLoginAccountRepository accounts(connection);
  eq2::login::LiveLoginService service(
      eq2::login::LoginServerConfig{.address = "127.0.0.1", .port = 0},
      accounts,
      supported_login_versions());

  require(service.start(), "live login starts TCP world listener and UDP client listener");
  require(service.port() != 0, "live login binds a shared TCP/UDP login port");

  const auto login_response =
      login_over_udp_session_handshake(service.port(), "tester", "correct", 546);
  require(login_response.has_value(),
          "live login writes valid login reply after real UDP session handshake");
  if (login_response.has_value()) {
    require_eq(decode_reply_account_id(*login_response).value_or(0), std::uint32_t{42},
               "live login UDP reply carries SQL-backed account id");
  }
  require(wait_for_login_count(service, 1), "live login service handles UDP login frame");

  const auto outcomes = service.login_outcomes();
  require_eq(outcomes.front().status, eq2::login::LoginPacketStatus::accepted,
             "live login accepts valid UDP credentials");
  require(outcomes.front().account.has_value(), "live login UDP path returns SQL-backed account");
  require_eq(outcomes.front().account->id, 42, "live login UDP path reads account id");

  service.stop();
}

void live_login_slice_accepts_configured_one_byte_login_opcodes() {
  auto connection = std::make_shared<ScriptedLoginConnection>();
  eq2::db::SqlLoginAccountRepository accounts(connection);
  const auto live_options = eq2::login::LiveLoginOptions{
      .requested_port = 0,
      .login_request_opcode = 0x01,
      .login_reply_opcode = 0x02,
      .world_list_reply_opcode = 0x03,
      .all_worlds_request_opcode = 0x04,
      .characters_request_opcode = 0x05,
      .characters_reply_opcode = 0x06,
      .opcode_width = eq2::protocol::ApplicationOpcodeWidth::one_byte,
  };
  eq2::login::LiveLoginService service(
      eq2::login::LoginServerConfig{.address = "127.0.0.1", .port = 0},
      accounts,
      supported_login_versions(),
      live_options);

  require(service.start(), "live login starts with configured one-byte login opcodes");
  require(service.port() != 0, "configured one-byte login binds a shared TCP/UDP login port");

  const auto login_response =
      login_over_udp_session_handshake(service.port(),
                                       "tester",
                                       "correct",
                                       546,
                                       live_options.login_request_opcode,
                                       live_options.opcode_width);
  require(login_response.has_value(),
          "configured one-byte login writes a reply after UDP session handshake");
  if (login_response.has_value()) {
    require_eq(decode_reply_account_id(*login_response,
                                       live_options.login_reply_opcode,
                                       live_options.opcode_width)
                   .value_or(0),
               std::uint32_t{42},
               "configured one-byte login reply carries SQL-backed account id");
  }
  require(wait_for_login_count(service, 1),
          "configured one-byte login service handles the login frame");

  const auto outcomes = service.login_outcomes();
  require_eq(outcomes.front().status, eq2::login::LoginPacketStatus::accepted,
             "configured one-byte login accepts valid credentials");

  service.stop();
}

void live_login_registered_world_reaches_udp_world_list() {
  auto connection = std::make_shared<ScriptedLoginConnection>();
  eq2::db::SqlLoginAccountRepository accounts(connection);
  eq2::db::SqlWorldRegistrationRepository worlds(connection);
  eq2::login::LiveLoginService service(
      eq2::login::LoginServerConfig{.address = "127.0.0.1", .port = 0},
      accounts,
      worlds,
      supported_login_versions());

  require(service.start(), "live login world-list test starts TCP and UDP listeners");
  require(service.port() != 0, "live login world-list test binds shared port");

  eq2::net::TcpSocketClient world_client;
  require(world_client.connect_to(eq2::net::SocketEndpoint{.address = "127.0.0.1",
                                                           .port = service.port()}),
          "legacy world registration client connects over TCP");
  require(world_client.send(make_ls_info_frame("world-account", "secret")),
          "legacy world registration client sends LSInfo frame");
  require(wait_for_event(service, eq2::login::LiveLoginEventType::world_registered),
          "live login accepts world registration over real TCP");

  const auto registrations = service.world_registration_results();
  require(!registrations.empty(), "live login records world registration result");
  if (!registrations.empty()) {
    require_eq(registrations.front().status, eq2::login::WorldRegistrationStatus::accepted,
               "live login accepts scripted world account");
    require(registrations.front().world.has_value(),
            "accepted world registration exposes registered world");
  }

  eq2::net::UdpSocketClient login_client;
  require(login_client.connect_to(eq2::net::SocketEndpoint{.address = "127.0.0.1",
                                                           .port = service.port()}),
          "login client connects to UDP listener after world registration");

  const auto session_request = eq2::protocol::encode_protocol_packet(
      eq2::protocol::kOpSessionRequest,
      eq2::protocol::encode_session_request(eq2::protocol::SessionRequest{
          .unknown_a = 0,
          .session = 0x10203040,
          .max_length = 512,
      }));
  require(login_client.send(session_request),
          "login client sends session request before world-list login");
  const auto session_response = login_client.receive();
  require(session_response.has_value(), "login client receives session response");

  require(login_client.send(make_sequenced_live_login_frame("tester", "correct", 546, 0)),
          "login client sends sequenced login frame after world registration");

  auto account_id = std::optional<std::uint32_t>{};
  for (auto attempt = 0; attempt < 6 && !account_id.has_value(); ++attempt) {
    auto response = login_client.receive();
    if (!response.has_value()) {
      break;
    }

    const auto protocol = eq2::protocol::decode_protocol_packet(*response);
    if (protocol.has_value() && protocol->opcode == eq2::protocol::kOpAck) {
      continue;
    }

    if (!account_id.has_value()) {
      account_id = decode_reply_account_id(*response);
    }
  }

  require_eq(account_id.value_or(0), static_cast<std::uint32_t>(42),
             "login client receives accepted login reply before world list");
  require(login_client.send(make_sequenced_all_worlds_request_frame(1)),
          "login client sends legacy all-worlds description request");

  auto world_list = std::optional<eq2::protocol::LoginWorldListPayload>{};
  auto character_list_account_id = std::optional<std::uint32_t>{};
  auto post_world_login_reply = std::optional<std::uint8_t>{};
  for (auto attempt = 0; attempt < 8 &&
                         (!world_list.has_value() || !character_list_account_id.has_value() ||
                          !post_world_login_reply.has_value());
       ++attempt) {
    auto response = login_client.receive();
    if (!response.has_value()) {
      break;
    }

    const auto protocol = eq2::protocol::decode_protocol_packet(*response);
    if (protocol.has_value() && protocol->opcode == eq2::protocol::kOpAck) {
      continue;
    }

    if (!world_list.has_value()) {
      world_list = decode_world_list_response(*response);
    }
    if (!character_list_account_id.has_value()) {
      character_list_account_id = decode_character_list_account_id(*response);
    }
    if (!post_world_login_reply.has_value()) {
      const auto code = decode_login_reply_code(*response);
      if (code.has_value() && *code == 10) {
        post_world_login_reply = code;
      }
    }
  }

  require(world_list.has_value(), "login client receives world-list response after login");
  if (world_list.has_value()) {
    require_eq(world_list->worlds.size(), static_cast<std::size_t>(1),
               "world-list response includes registered world");
    require_eq(world_list->worlds.front().world_id, 77,
               "world-list response uses registered world account id");
    require_eq(world_list->worlds.front().display_name, std::string_view("Public World"),
               "world-list response uses world display name from DB");
    require(!world_list->worlds.front().locked,
            "world-list response exposes unlocked registered world");
  }
  require_eq(character_list_account_id.value_or(0), static_cast<std::uint32_t>(42),
             "login client receives empty character-list response for logged-in account");
  require_eq(post_world_login_reply.value_or(0), static_cast<std::uint8_t>(10),
             "login client receives legacy post-world-list login flag reply");
  require(wait_for_login_count(service, 1),
          "live login handles UDP login after world registration");

  login_client.close();
  world_client.close();
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

void live_login_slice_processes_simultaneous_tcp_clients() {
  auto connection = std::make_shared<ScriptedLoginConnection>();
  eq2::db::SqlLoginAccountRepository accounts(connection);
  eq2::login::LiveLoginService service(
      eq2::login::LoginServerConfig{.address = "127.0.0.1", .port = 0},
      accounts,
      supported_login_versions());

  require(service.start(), "live login concurrent test starts real loopback TCP transport");

  eq2::net::TcpSocketClient first_client;
  eq2::net::TcpSocketClient second_client;
  require(open_login_session(first_client, service.port()),
          "first live login concurrent client completes session handshake");
  require(open_login_session(second_client, service.port()),
          "second live login concurrent client completes session handshake");

  require(first_client.send(make_sequenced_live_login_frame("tester", "correct", 546, 0)),
          "first live login concurrent client sends login frame");
  require(second_client.send(make_sequenced_live_login_frame("tester", "correct", 546, 0)),
          "second live login concurrent client sends login frame");

  const auto first_response = first_client.receive();
  const auto second_response = second_client.receive();
  require(first_response.has_value(), "first live login concurrent client receives reply");
  require(second_response.has_value(), "second live login concurrent client receives reply");
  if (first_response.has_value()) {
    require_eq(decode_reply_account_id(*first_response).value_or(0), std::uint32_t{42},
               "first live login concurrent reply carries account id");
  }
  if (second_response.has_value()) {
    require_eq(decode_reply_account_id(*second_response).value_or(0), std::uint32_t{42},
               "second live login concurrent reply carries account id");
  }
  require(wait_for_login_count(service, 2),
          "live login service handles two simultaneous TCP clients");
  const auto outcomes = service.login_outcomes();
  const auto duplicate_count = static_cast<std::size_t>(
      std::count_if(outcomes.begin(), outcomes.begin() + 2, [](const auto& outcome) {
        return outcome.should_disconnect_existing_session;
      }));
  require_eq(duplicate_count, static_cast<std::size_t>(1),
             "live login flags exactly one active duplicate account session");

  first_client.close();
  second_client.close();
  require(wait_for_event_count(service, eq2::login::LiveLoginEventType::disconnected, 2),
          "live login records both concurrent client disconnects");

  const auto reconnect_response =
      login_over_session_handshake(service.port(), "tester", "correct", 546);
  require(reconnect_response.has_value(), "live login accepts clean reconnect after disconnects");
  require(wait_for_login_count(service, 3), "live login handles clean reconnect after disconnects");
  const auto reconnect_outcomes = service.login_outcomes();
  require(!reconnect_outcomes[2].should_disconnect_existing_session,
          "live login releases active account state after disconnect");

  service.stop();
}

void live_login_same_session_relogin_does_not_create_duplicate_state() {
  auto connection = std::make_shared<ScriptedLoginConnection>();
  eq2::db::SqlLoginAccountRepository accounts(connection);
  eq2::login::LiveLoginService service(
      eq2::login::LoginServerConfig{.address = "127.0.0.1", .port = 0},
      accounts,
      supported_login_versions());

  require(service.start(), "live login same-session relogin test starts TCP transport");

  eq2::net::TcpSocketClient client;
  require(open_login_session(client, service.port()),
          "same-session relogin client completes session handshake");
  require(client.send(make_sequenced_live_login_frame("tester", "correct", 546, 0)),
          "same-session relogin client sends first login frame");
  const auto first_response = client.receive();
  require(first_response.has_value(), "same-session relogin client receives first reply");
  require(client.send(make_sequenced_live_login_frame("tester", "correct", 546, 1)),
          "same-session relogin client sends second login frame");
  const auto second_response = client.receive();
  require(second_response.has_value(), "same-session relogin client receives second reply");
  require(wait_for_login_count(service, 2),
          "live login handles same-session relogin frames");

  const auto outcomes = service.login_outcomes();
  require(!outcomes[0].should_disconnect_existing_session,
          "first same-session login is not a duplicate");
  require(!outcomes[1].should_disconnect_existing_session,
          "second same-session login does not create duplicate state");

  client.close();
  require(wait_for_event_count(service, eq2::login::LiveLoginEventType::disconnected, 1),
          "same-session relogin client disconnect releases session state");

  const auto reconnect_response =
      login_over_session_handshake(service.port(), "tester", "correct", 546);
  require(reconnect_response.has_value(),
          "same-session relogin allows clean reconnect after disconnect");
  require(wait_for_login_count(service, 3),
          "live login handles reconnect after same-session relogin disconnect");
  const auto reconnect_outcomes = service.login_outcomes();
  require(!reconnect_outcomes[2].should_disconnect_existing_session,
          "same-session relogin does not poison later reconnect duplicate state");

  service.stop();
}

void live_login_protocol_disconnect_releases_account_state() {
  auto connection = std::make_shared<ScriptedLoginConnection>();
  eq2::db::SqlLoginAccountRepository accounts(connection);
  eq2::login::LiveLoginService service(
      eq2::login::LoginServerConfig{.address = "127.0.0.1", .port = 0},
      accounts,
      supported_login_versions());

  require(service.start(), "live login protocol disconnect test starts TCP transport");

  eq2::net::TcpSocketClient first_client;
  require(open_login_session(first_client, service.port()),
          "protocol disconnect client completes session handshake");
  require(first_client.send(make_sequenced_live_login_frame("tester", "correct", 546, 0)),
          "protocol disconnect client sends login frame");
  const auto first_response = first_client.receive();
  require(first_response.has_value(), "protocol disconnect client receives login reply");
  require(wait_for_login_count(service, 1),
          "live login handles pre-disconnect login frame");

  const std::vector<std::uint8_t> empty_payload;
  const auto disconnect = eq2::protocol::encode_protocol_packet(
      eq2::protocol::kOpSessionDisconnect, empty_payload);
  require(first_client.send(disconnect),
          "protocol disconnect client sends session disconnect frame");
  require(wait_for_event_count(service, eq2::login::LiveLoginEventType::disconnected, 1),
          "live login records protocol-level disconnect");

  const auto second_response =
      login_over_session_handshake(service.port(), "tester", "correct", 546);
  require(second_response.has_value(),
          "live login accepts reconnect after protocol-level disconnect");
  require(wait_for_login_count(service, 2),
          "live login handles reconnect after protocol-level disconnect");
  const auto outcomes = service.login_outcomes();
  require(!outcomes[1].should_disconnect_existing_session,
          "protocol-level disconnect releases active account state");

  first_client.close();
  service.stop();
}

}  // namespace

int main() {
  login_server_config_loads_from_core_config();
  client_login_reaches_legacy_success_outcome_through_source2_boundaries();
  unsupported_client_version_reaches_legacy_bad_version_outcome();
  world_registration_uses_protocol_frame_and_db_repository();
  live_login_slice_uses_real_tcp_protocol_pipeline_and_sql_repository();
  live_login_slice_accepts_udp_client_login();
  live_login_slice_accepts_configured_one_byte_login_opcodes();
  live_login_registered_world_reaches_udp_world_list();
  live_login_slice_can_create_accounts_when_policy_allows_it();
  live_login_slice_processes_simultaneous_tcp_clients();
  live_login_same_session_relogin_does_not_create_duplicate_state();
  live_login_protocol_disconnect_releases_account_state();

  if (failures != 0) {
    std::cerr << failures << " login server assertion(s) failed\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

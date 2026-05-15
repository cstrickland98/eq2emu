#include <eq2/core/config.h>
#include <eq2/db/fake_database.h>
#include <eq2/db/query.h>
#include <eq2/db/sql_repositories.h>
#include <eq2/login/live_login.h>
#include <eq2/protocol/interserver_packet.h>
#include <eq2/protocol/login_world.h>
#include <eq2/protocol/protocol_packet.h>
#include <eq2/protocol/session.h>
#include <eq2/world/live_world.h>
#include <eq2/world/session.h>
#include <eq2/world/zone_handoff_adapter.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
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

class CapturingZoneHandoff final : public eq2::world::ZoneHandoff {
 public:
  auto request_zone_entry(const eq2::world::ZoneHandoffRequest& request)
      -> eq2::world::ZoneHandoffResult override {
    last_request = request;
    return eq2::world::ZoneHandoffResult{
        .accepted = true,
    };
  }

  std::optional<eq2::world::ZoneHandoffRequest> last_request;
};

class LoginRegistrationConnection final : public eq2::db::QueryConnection {
 public:
  auto execute(const eq2::db::QueryRequest& request)
      -> eq2::core::Result<eq2::db::QueryResult> override {
    requests.push_back(request);

    if (request.sql.find("login_versions") != std::string::npos) {
      return rows({eq2::db::QueryRow{.columns = {{"version", "2026.05.14"}}}});
    }

    if (request.sql.find("account = ? and password") != std::string::npos) {
      return rows({eq2::db::QueryRow{.columns = {{"id", "77"}}}});
    }

    if (request.sql.find("select disabled") != std::string::npos) {
      return rows({eq2::db::QueryRow{.columns = {{"disabled", "0"}}}});
    }

    if (request.sql.find("select name from login_worldservers") != std::string::npos) {
      return rows({eq2::db::QueryRow{.columns = {{"name", "Public World"}}}});
    }

    return rows({});
  }

  std::vector<eq2::db::QueryRequest> requests;

 private:
  static auto rows(std::vector<eq2::db::QueryRow> rows)
      -> eq2::core::Result<eq2::db::QueryResult> {
    return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{
        .rows = std::move(rows),
        .affected_rows = 1,
    });
  }
};

class CharacterListConnection final : public eq2::db::QueryConnection {
 public:
  auto execute(const eq2::db::QueryRequest& request)
      -> eq2::core::Result<eq2::db::QueryResult> override {
    requests.push_back(request);
    if (request.sql.find("from login_characters") == std::string::npos) {
      return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{});
    }

    return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{
        .rows =
            {
                eq2::db::QueryRow{.columns =
                                      {
                                          {"id", "1001"},
                                          {"character_id", "2002"},
                                          {"server_id", "77"},
                                          {"name", "Alys"},
                                          {"race", "1"},
                                          {"class", "2"},
                                          {"gender", "3"},
                                          {"current_zone_id", "10"},
                                          {"level", "12"},
                                      }},
            },
        .affected_rows = 1,
    });
  }

  std::vector<eq2::db::QueryRequest> requests;
};

class ZoneBootstrapConnection final : public eq2::db::QueryConnection {
 public:
  auto execute(const eq2::db::QueryRequest& request)
      -> eq2::core::Result<eq2::db::QueryResult> override {
    requests.push_back(request);
    if (request.parameters.empty() || request.parameters.front() != "10") {
      return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{});
    }

    return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{
        .rows =
            {
                eq2::db::QueryRow{.columns =
                                      {
                                          {"id", "10"},
                                          {"name", "qeynos"},
                                          {"safe_x", "1.5"},
                                          {"safe_y", "2.5"},
                                          {"safe_z", "3.5"},
                                          {"safe_heading", "4.5"},
                                      }},
            },
        .affected_rows = 1,
    });
  }

  std::vector<eq2::db::QueryRequest> requests;
};

auto supported_login_versions() -> eq2::protocol::OpcodeVersionRanges {
  eq2::protocol::OpcodeVersionRanges ranges;
  ranges.add_range(546, 561);
  return ranges;
}

auto wait_for_world_registration(const eq2::login::LiveLoginService& login, std::size_t count)
    -> bool {
  for (auto attempt = 0; attempt < 80; ++attempt) {
    if (login.world_registration_results().size() >= count) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  return false;
}

auto wait_for_world_event(const eq2::world::LiveWorldService& world,
                          eq2::world::LiveWorldEventType type) -> bool {
  for (auto attempt = 0; attempt < 80; ++attempt) {
    const auto events = world.events();
    for (const auto& event : events) {
      if (event.type == type) {
        return true;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  return false;
}

void world_config_loads_source2_runtime_settings() {
  eq2::core::MapConfig values;
  values.set("world.address", "127.0.0.1");
  values.set("world.port", "9200");
  values.set("login.address", "127.0.0.2");
  values.set("login.port", "9102");
  values.set("world.name", "Public World");
  values.set("world.account", "world-account");
  values.set("world.password", "secret");
  values.set("world.server_version", "2026.05.14");

  const auto config = eq2::world::load_world_server_config(values);

  require_eq(config.address, std::string_view("127.0.0.1"), "world config reads bind address");
  require_eq(config.port, static_cast<std::uint16_t>(9200), "world config reads bind port");
  require_eq(config.login_address, std::string_view("127.0.0.2"), "world config reads login address");
  require_eq(config.login_port, static_cast<std::uint16_t>(9102), "world config reads login port");
  require_eq(config.world_name, std::string_view("Public World"), "world config reads world name");
}

void world_registration_frame_matches_protocol_boundary() {
  const auto frame = eq2::world::make_world_registration_frame(eq2::world::WorldServerConfig{
      .address = "127.0.0.1",
      .world_name = "Public World",
      .world_account = "world-account",
      .world_password = "secret",
      .protocol_version = "0.5.0",
      .server_version = "2026.05.14",
      .database_version = 1234,
  });

  require(frame.has_value(), "world registration frame encodes");
  const auto decoded_frame = eq2::protocol::decode_interserver_packet(*frame);
  require(decoded_frame.has_value(), "world registration frame decodes");
  require_eq(decoded_frame->opcode, eq2::protocol::kServerOpLsInfo,
             "world registration uses LSInfo opcode");
  const auto info = eq2::protocol::decode_server_ls_info_payload(decoded_frame->payload);
  require(info.has_value(), "world registration LSInfo payload decodes");
  require_eq(info->world_name, std::string_view("Public World"),
             "world registration payload carries world name");
  require_eq(info->account, std::string_view("world-account"),
             "world registration payload carries account");
}

void character_list_and_select_match_known_account_rows() {
  eq2::db::FakeCharacterListRepository characters;
  characters.characters_by_account[42] = {
      eq2::db::CharacterListRecord{
          .login_character_id = 1001,
          .character_id = 2002,
          .server_id = 77,
          .name = "Alys",
          .current_zone_id = 10,
          .level = 12,
      },
  };
  CapturingZoneHandoff zone_handoff;
  eq2::world::WorldServer world(eq2::world::WorldServerConfig{}, characters, zone_handoff);

  world.start();
  const auto session_id = world.accept_client();
  require(session_id.has_value(), "world server accepts client sessions through net");
  const auto session = world.open_session(*session_id, 42, 0x12345678);

  const auto list = world.character_list(session);
  require_eq(list.size(), static_cast<std::size_t>(1), "world returns one known character");
  require_eq(list.front().name, std::string_view("Alys"), "world character list preserves name");
  require_eq(list.front().current_zone_id, 10, "world character list preserves zone id");

  const auto selected = world.select_character(session, 2002);
  require(selected.accepted, "world accepts known character selection");
  require_eq(selected.character.name, std::string_view("Alys"),
             "world select returns selected character summary");
  require(!world.select_character(session, 9999).accepted,
          "world rejects characters outside the authenticated account list");
}

void zone_handoff_uses_interface_without_zone_internals() {
  eq2::db::FakeCharacterListRepository characters;
  CapturingZoneHandoff zone_handoff;
  eq2::world::WorldServer world(eq2::world::WorldServerConfig{}, characters, zone_handoff);

  const auto session = eq2::world::WorldClientSession{
      .session = eq2::net::SessionId{1},
      .account_id = 42,
      .access_key = 0x12345678,
      .authenticated = true,
  };
  const auto character = eq2::world::CharacterSummary{
      .character_id = 2002,
      .server_id = 77,
      .name = "Alys",
      .level = 12,
      .current_zone_id = 10,
  };

  const auto result = world.handoff_to_zone(session, character);

  require(result.accepted, "world accepts zone handoff through interface");
  require(zone_handoff.last_request.has_value(), "zone handoff interface receives a request");
  require_eq(zone_handoff.last_request->account_id, 42, "zone handoff carries account id");
  require_eq(zone_handoff.last_request->character_id, 2002, "zone handoff carries character id");
  require_eq(zone_handoff.last_request->zone_id, 10, "zone handoff carries zone id");
  require_eq(zone_handoff.last_request->access_key, 0x12345678,
             "zone handoff carries access key");
}

void live_world_registers_with_login_loads_characters_and_requests_zone_handoff() {
  auto login_connection = std::make_shared<LoginRegistrationConnection>();
  eq2::db::SqlLoginAccountRepository login_accounts(login_connection);
  eq2::db::SqlWorldRegistrationRepository login_worlds(login_connection);
  eq2::login::LiveLoginService login(
      eq2::login::LoginServerConfig{.address = "127.0.0.1", .port = 0},
      login_accounts,
      login_worlds,
      supported_login_versions());

  require(login.start(), "live source2 login starts for world registration smoke");

  auto character_connection = std::make_shared<CharacterListConnection>();
  eq2::db::SqlCharacterListRepository characters(character_connection);
  CapturingZoneHandoff zone_handoff;
  eq2::world::LiveWorldService world(
      eq2::world::WorldServerConfig{
          .address = "127.0.0.1",
          .port = 0,
          .login_address = "127.0.0.1",
          .login_port = login.port(),
          .world_name = "Public World",
          .world_account = "world-account",
          .world_password = "secret",
          .protocol_version = "0.5.0",
          .server_version = "2026.05.14",
          .database_version = 1234,
      },
      characters,
      zone_handoff);

  require(world.start(), "live world starts real loopback TCP transport");
  require(world.register_with_login(), "live world sends LSInfo to live source2 login over TCP");
  require(wait_for_world_registration(login, 1),
          "live source2 login receives world registration over real transport");
  const auto registrations = login.world_registration_results();
  require_eq(registrations.front().status, eq2::login::WorldRegistrationStatus::accepted,
             "live source2 login accepts SQL-backed world registration");
  require(registrations.front().world.has_value(), "accepted live world registration returns world data");
  require_eq(registrations.front().world->account_id, 77,
             "accepted live world registration maps world account id");

  const auto session_request = eq2::protocol::encode_session_request(eq2::protocol::SessionRequest{
      .unknown_a = 0,
      .session = 0x12345678,
      .max_length = 512,
  });
  const auto session_packet = eq2::protocol::encode_protocol_packet(
      eq2::protocol::kOpSessionRequest, session_request);
  require(eq2::net::send_tcp_loopback(world.port(), session_packet),
          "live world accepts a real client protocol connection");
  require(wait_for_world_event(world, eq2::world::LiveWorldEventType::session_requested),
          "live world runs client bytes through stream pipeline");

  const auto handoff_response = world.admit_login_handoff(eq2::protocol::UserToWorldRequest{
      .login_account_id = 42,
      .character_id = 2002,
      .world_id = 77,
      .from_id = 100,
      .to_id = 200,
      .ip_address = "127.0.0.1",
  });
  require(handoff_response.access_key != 0, "world creates access key for login handoff");
  require(!world.open_session_from_handoff(eq2::net::SessionId{9}, 42,
                                           handoff_response.access_key + 1)
               .has_value(),
          "world rejects sessions with bad access key");

  auto session = world.open_session_from_handoff(eq2::net::SessionId{10}, 42,
                                                 handoff_response.access_key);
  require(session.has_value(), "world opens authenticated session from login handoff");

  const auto list = world.character_list(*session);
  require_eq(list.size(), static_cast<std::size_t>(1),
             "live world loads character list through SQL repository");
  require_eq(list.front().name, std::string_view("Alys"),
             "live world preserves character list mapping");
  require_eq(character_connection->requests.front().parameters.front(), std::string_view("42"),
             "live world passes account id through SQL character repository");

  const auto selected = world.select_character(*session, 2002);
  require(selected.accepted, "live world accepts selected account-owned character");
  const auto handoff = world.handoff_to_zone(*session, selected.character);
  require(handoff.accepted, "live world requests zone admission through boundary");
  require(zone_handoff.last_request.has_value(), "zone handoff boundary receives live request");
  require_eq(zone_handoff.last_request->character_id, 2002,
             "live zone handoff carries selected character id");
  require_eq(zone_handoff.last_request->zone_id, 10,
             "live zone handoff carries selected character zone id");

  world.stop();
  login.stop();
}

void world_zone_handoff_admits_selected_character_to_db_bootstrapped_zone_runtime() {
  auto character_connection = std::make_shared<CharacterListConnection>();
  eq2::db::SqlCharacterListRepository characters(character_connection);
  auto zone_connection = std::make_shared<ZoneBootstrapConnection>();
  eq2::db::SqlZoneBootstrapRepository zone_repository(zone_connection);
  eq2::zone::ZoneBootstrapService zones(zone_repository);
  eq2::world::ZoneRuntimeHandoff zone_handoff(zones);
  eq2::world::WorldServer world(eq2::world::WorldServerConfig{}, characters, zone_handoff);

  const auto session = world.open_session(eq2::net::SessionId{1}, 42, 0x12345678);
  const auto selected = world.select_character(session, 2002);
  require(selected.accepted, "world selects SQL-backed character for zone runtime admission");

  const auto handoff = world.handoff_to_zone(session, selected.character);
  require(handoff.accepted, "world zone handoff adapter admits character into zone runtime");

  const auto snapshot = zones.snapshot(eq2::zone::ZoneId{10});
  require(snapshot.has_value(), "zone runtime snapshot is available after world handoff");
  require_eq(snapshot->spawns.size(), static_cast<std::size_t>(1),
             "zone runtime contains admitted player spawn");
  require_eq(snapshot->spawns.front().id.value, 2002,
             "zone runtime player spawn uses selected character id");
  require_eq(snapshot->spawns.front().position.x, 1.5F,
             "zone runtime player spawn uses DB safe location");
  require_eq(zone_connection->requests.front().parameters.front(), std::string_view("10"),
             "world zone handoff loads zone metadata through SQL repository");
}

}  // namespace

int main() {
  world_config_loads_source2_runtime_settings();
  world_registration_frame_matches_protocol_boundary();
  character_list_and_select_match_known_account_rows();
  zone_handoff_uses_interface_without_zone_internals();
  live_world_registers_with_login_loads_characters_and_requests_zone_handoff();
  world_zone_handoff_admits_selected_character_to_db_bootstrapped_zone_runtime();

  if (failures != 0) {
    std::cerr << failures << " world session assertion(s) failed\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

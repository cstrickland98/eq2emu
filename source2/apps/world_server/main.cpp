#include <eq2/core/config.h>
#include <eq2/core/log.h>
#include <eq2/core/runtime_config.h>
#include <eq2/core/version.h>
#include <eq2/db/query.h>
#include <eq2/db/sql_repositories.h>
#include <eq2/login/live_login.h>
#include <eq2/protocol/login_world.h>
#include <eq2/protocol/opcode_version.h>
#include <eq2/world/live_world.h>
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

class SmokeLoginWorldConnection final : public eq2::db::QueryConnection {
 public:
  auto execute(const eq2::db::QueryRequest& request)
      -> eq2::core::Result<eq2::db::QueryResult> override {
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

 private:
  static auto rows(std::vector<eq2::db::QueryRow> rows)
      -> eq2::core::Result<eq2::db::QueryResult> {
    return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{
        .rows = std::move(rows),
        .affected_rows = 1,
    });
  }
};

class SmokeCharacterConnection final : public eq2::db::QueryConnection {
 public:
  auto execute(const eq2::db::QueryRequest& request)
      -> eq2::core::Result<eq2::db::QueryResult> override {
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
};

class SmokeZoneConnection final : public eq2::db::QueryConnection {
 public:
  auto execute(const eq2::db::QueryRequest& request)
      -> eq2::core::Result<eq2::db::QueryResult> override {
    if (request.sql.find("from zones") == std::string::npos || request.parameters.empty() ||
        request.parameters.front() != "10") {
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
};

class SmokeZoneHandoff final : public eq2::world::ZoneHandoff {
 public:
  auto request_zone_entry(const eq2::world::ZoneHandoffRequest& request)
      -> eq2::world::ZoneHandoffResult override {
    last_request = request;
    return eq2::world::ZoneHandoffResult{.accepted = true};
  }

  std::optional<eq2::world::ZoneHandoffRequest> last_request;
};

auto supported_login_versions() -> eq2::protocol::OpcodeVersionRanges {
  eq2::protocol::OpcodeVersionRanges ranges;
  ranges.add_range(546, 561);
  return ranges;
}

auto wait_for_world_registration(const eq2::login::LiveLoginService& login) -> bool {
  for (auto attempt = 0; attempt < 80; ++attempt) {
    if (!login.world_registration_results().empty()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  return false;
}

auto run_live_smoke() -> int {
  auto login_connection = std::make_shared<SmokeLoginWorldConnection>();
  eq2::db::SqlLoginAccountRepository login_accounts(login_connection);
  eq2::db::SqlWorldRegistrationRepository login_worlds(login_connection);
  eq2::login::LiveLoginService login(
      eq2::login::LoginServerConfig{.address = "127.0.0.1", .port = 0},
      login_accounts,
      login_worlds,
      supported_login_versions());

  if (!login.start()) {
    std::cerr << "failed to start source2 login receiver\n";
    return EXIT_FAILURE;
  }

  auto character_connection = std::make_shared<SmokeCharacterConnection>();
  eq2::db::SqlCharacterListRepository characters(character_connection);
  auto zone_connection = std::make_shared<SmokeZoneConnection>();
  eq2::db::SqlZoneBootstrapRepository zone_repository(zone_connection);
  eq2::zone::ZoneBootstrapService zones(zone_repository);
  eq2::world::ZoneRuntimeHandoff zone_handoff(zones);
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

  if (!world.start() || !world.register_with_login() || !wait_for_world_registration(login)) {
    world.stop();
    login.stop();
    std::cerr << "world live smoke registration failed\n";
    return EXIT_FAILURE;
  }

  const auto response = world.admit_login_handoff(eq2::protocol::UserToWorldRequest{
      .login_account_id = 42,
      .character_id = 2002,
      .world_id = 77,
      .from_id = 1,
      .to_id = 2,
      .ip_address = "127.0.0.1",
  });
  auto session = world.open_session_from_handoff(eq2::net::SessionId{1}, 42, response.access_key);
  if (!session.has_value()) {
    world.stop();
    login.stop();
    std::cerr << "world live smoke handoff failed\n";
    return EXIT_FAILURE;
  }

  const auto characters_for_account = world.character_list(*session);
  const auto selected = world.select_character(*session, 2002);
  auto zone_handoff_result = world.handoff_to_zone(*session, selected.character);
  const auto zone_snapshot = zones.snapshot(eq2::zone::ZoneId{10});
  if (characters_for_account.empty() || !selected.accepted ||
      !zone_handoff_result.accepted || !zone_snapshot.has_value() ||
      zone_snapshot->spawns.empty()) {
    world.stop();
    login.stop();
    std::cerr << "world live smoke character flow failed\n";
    return EXIT_FAILURE;
  }

  world.stop();
  login.stop();

  std::cout << "smoke-world-live registered world=77 character="
            << selected.character.character_id << " zone="
            << selected.character.current_zone_id << " spawn="
            << zone_snapshot->spawns.front().id.value << '\n';
  return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::string_view(argv[1]) == "--smoke-world-live") {
    return run_live_smoke();
  }

  eq2::core::MapConfig config_values;
  if (argc > 1 && std::string_view(argv[1]) == "--strict-config") {
    config_values.set("world.port", "invalid");
  }

  auto runtime = eq2::core::load_runtime_config(config_values);
  if (!runtime.has_value()) {
    std::cerr << runtime.error().message << '\n';
    return 1;
  }

  eq2::core::ConsoleLogSink log;
  eq2::core::log(log, eq2::core::LogLevel::info, "world",
                 "source2 world composition root validated");

  std::cout << "eq2_world_server source2 wiring "
            << "version=" << eq2::core::kSource2Version << ' '
            << runtime.value().world_listen.address << ':' << runtime.value().world_listen.port
            << " login=" << runtime.value().login_remote.address << ':'
            << runtime.value().login_remote.port << '\n';
  return 0;
}

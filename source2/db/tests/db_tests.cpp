#include <eq2/db/config.h>
#include <eq2/db/fake_database.h>
#include <eq2/db/query.h>
#include <eq2/db/repositories.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
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

class ThreadRecordingConnection final : public eq2::db::QueryConnection {
 public:
  auto execute(const eq2::db::QueryRequest& request) -> eq2::core::Result<eq2::db::QueryResult> override {
    executed_on = std::this_thread::get_id();
    executed_sql = request.sql;
    return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{
        .rows =
            {
                eq2::db::QueryRow{.columns = {{"value", "ok"}}},
            },
        .affected_rows = 1,
    });
  }

  std::thread::id executed_on;
  std::string executed_sql;
};

void database_config_captures_connection_settings() {
  const eq2::db::DatabaseConfig config{
      .host = "db.local",
      .port = 3307,
      .database = "eq2login",
      .username = "eq2",
      .password = "secret",
      .max_connections = 8,
  };

  require_eq(config.host, std::string_view("db.local"), "config stores host");
  require_eq(config.port, static_cast<std::uint16_t>(3307), "config stores port");
  require_eq(config.max_connections, static_cast<std::uint16_t>(8), "config stores pool size");
}

void connection_pool_round_robins_connections() {
  auto first = std::make_shared<eq2::db::FakeQueryConnection>();
  auto second = std::make_shared<eq2::db::FakeQueryConnection>();
  eq2::db::ConnectionPool pool({first, second});

  require_eq(pool.size(), static_cast<std::size_t>(2), "pool reports connection count");
  require(pool.acquire() == first, "first acquire returns first connection");
  require(pool.acquire() == second, "second acquire returns second connection");
  require(pool.acquire() == first, "third acquire wraps to first connection");
}

void async_executor_runs_queries_off_the_owner_thread() {
  const auto owner_thread = std::this_thread::get_id();
  auto connection = std::make_shared<ThreadRecordingConnection>();
  eq2::db::AsyncQueryExecutor executor(1);

  auto future = executor.execute(connection, eq2::db::QueryRequest{
                                                .sql = "select account_id from account where name=?",
                                                .parameters = {"tester"},
                                            });
  const auto result = future.get();
  executor.stop();

  require(result.has_value(), "async query returns a result");
  require_eq(result.value().rows.front().get("value").value_or(""), std::string_view("ok"),
             "async query returns row values");
  require_eq(connection->executed_sql, std::string_view("select account_id from account where name=?"),
             "async query executes requested SQL");
  require(connection->executed_on != owner_thread, "async query runs outside owner thread");
}

void fake_repositories_cover_critical_login_world_character_and_zone_boundaries() {
  eq2::db::FakeLoginAccountRepository accounts;
  accounts.add_account(42, "tester", "correct");
  const auto account = accounts.find_by_name_and_password("tester", "correct");
  require(account.has_value(), "fake login account repository finds matching account");
  require_eq(account->id, 42, "fake login account carries id");
  require(!accounts.find_by_name_and_password("tester", "wrong").has_value(),
          "fake login account rejects wrong password");

  eq2::db::FakeWorldRegistrationRepository worlds;
  worlds.accounts["world-account"] = eq2::db::WorldAccountRecord{
      .id = 77,
      .account = "world-account",
      .display_name = "Public World",
      .password = "secret",
  };
  worlds.allowed_server_versions.insert("2026.05.14");
  require(worlds.server_version_is_allowed("2026.05.14"),
          "fake world repository accepts configured server version");
  require_eq(worlds.check_server_account("world-account", "secret"), 77,
             "fake world repository validates account password");
  require_eq(worlds.display_name_for_account(77), std::string_view("Public World"),
             "fake world repository returns display name");

  eq2::db::FakeCharacterListRepository characters;
  characters.characters_by_account[42] = {
      eq2::db::CharacterListRecord{
          .login_character_id = 1001,
          .character_id = 2002,
          .server_id = 77,
          .name = "Alys",
          .level = 12,
      },
  };
  require_eq(characters.load_character_list(42).front().name, std::string_view("Alys"),
             "fake character repository returns character list rows");

  eq2::db::FakeZoneBootstrapRepository zones;
  zones.zones[10] = eq2::db::ZoneBootstrapRecord{
      .zone_id = 10,
      .name = "qeynos",
      .safe_x = 1.0F,
      .safe_y = 2.0F,
      .safe_z = 3.0F,
      .safe_heading = 4.0F,
  };
  const auto zone = zones.load_zone(10);
  require(zone.has_value(), "fake zone repository returns zone bootstrap record");
  require_eq(zone->name, std::string_view("qeynos"), "fake zone repository preserves zone name");
  require(!zones.load_zone(99).has_value(), "fake zone repository reports missing zones");
}

}  // namespace

int main() {
  database_config_captures_connection_settings();
  connection_pool_round_robins_connections();
  async_executor_runs_queries_off_the_owner_thread();
  fake_repositories_cover_critical_login_world_character_and_zone_boundaries();

  if (failures != 0) {
    std::cerr << failures << " db assertion(s) failed\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

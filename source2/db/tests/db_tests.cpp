#include <eq2/db/config.h>
#include <eq2/db/fake_database.h>
#include <eq2/db/migrations.h>
#include <eq2/db/query.h>
#include <eq2/db/repositories.h>
#include <eq2/db/sql_repositories.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

class ScriptedConnection final : public eq2::db::QueryConnection {
 public:
  auto execute(const eq2::db::QueryRequest& request) -> eq2::core::Result<eq2::db::QueryResult> override {
    requests.push_back(request);
    if (!fail_on_sql_fragment.empty() &&
        request.sql.find(fail_on_sql_fragment) != std::string::npos) {
      return eq2::core::Result<eq2::db::QueryResult>::failure(eq2::core::Error{
          .code = eq2::core::ErrorCode::parse_error,
          .message = "scripted query failure",
      });
    }

    if (results.empty()) {
      return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{});
    }

    auto result = results.front();
    results.erase(results.begin());
    return eq2::core::Result<eq2::db::QueryResult>::success(std::move(result));
  }

  std::vector<eq2::db::QueryRequest> requests;
  std::vector<eq2::db::QueryResult> results;
  std::string fail_on_sql_fragment;
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

void sql_repositories_execute_expected_query_boundaries() {
  auto connection = std::make_shared<ScriptedConnection>();
  connection->results.push_back(eq2::db::QueryResult{
      .rows = {eq2::db::QueryRow{.columns = {{"id", "42"}, {"name", "tester"}}}},
  });

  eq2::db::SqlLoginAccountRepository accounts(connection);
  const auto account = accounts.find_by_name_and_password("tester", "correct");
  require(account.has_value(), "SQL account repository maps query row");
  require_eq(account->id, 42, "SQL account repository maps account id");
  require_eq(connection->requests.front().parameters.size(), static_cast<std::size_t>(2),
             "SQL account repository parameterizes username and password");

  connection->results.push_back(eq2::db::QueryResult{
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
  });
  eq2::db::SqlCharacterListRepository characters(connection);
  const auto list = characters.load_character_list(42);
  require_eq(list.size(), static_cast<std::size_t>(1),
             "SQL character repository maps character rows");
  require_eq(list.front().name, std::string_view("Alys"),
             "SQL character repository maps character name");
  require_eq(list.front().current_zone_id, 10,
             "SQL character repository maps zone id");

  connection->results.push_back(eq2::db::QueryResult{
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
  });
  eq2::db::SqlZoneBootstrapRepository zones(connection);
  const auto zone = zones.load_zone(10);
  require(zone.has_value(), "SQL zone repository maps zone row");
  require_eq(zone->name, std::string_view("qeynos"), "SQL zone repository maps zone name");

  eq2::db::MariaDbConnection disabled(eq2::db::DatabaseConfig{.database = "eq2"});
  const auto disabled_result = disabled.execute(eq2::db::QueryRequest{.sql = "select 1"});
  require(!disabled_result.has_value(), "disabled MariaDB adapter reports unavailable explicitly");
  require_eq(disabled_result.error().code, eq2::core::ErrorCode::unavailable,
             "disabled MariaDB adapter uses unavailable error");
}

void migration_discovery_orders_and_filters_pending_files() {
  namespace fs = std::filesystem;

  const auto root = fs::temp_directory_path() / "eq2emu_source2_migration_discovery_test";
  fs::remove_all(root);
  fs::create_directories(root);

  {
    std::ofstream(root / "202605150902_second.sql") << "select 2";
    std::ofstream(root / "202605150901_first.sql") << "select 1";
    std::ofstream(root / "notes.txt") << "ignored";
  }

  const auto migrations = eq2::db::discover_migrations(eq2::db::DatabaseKind::login, root);
  require_eq(migrations.size(), static_cast<std::size_t>(2),
             "migration discovery only returns SQL files");
  require_eq(migrations.front().id, std::string_view("202605150901_first"),
             "migration discovery sorts by lexical id");
  require(eq2::db::migration_id_is_valid(migrations.front().id),
          "timestamped migration names are accepted");
  require(!eq2::db::migration_id_is_valid("bad_name"),
          "non-timestamped migration names are rejected");

  eq2::db::MemoryAppliedMigrationStore applied;
  auto mark_first = applied.mark_applied(eq2::db::DatabaseKind::login, migrations.front().id);
  require(mark_first.has_value(), "memory migration store records applied ids");

  auto pending = eq2::db::pending_migrations(migrations, applied);
  require(pending.has_value(), "pending migration calculation succeeds");
  require_eq(pending.value().size(), static_cast<std::size_t>(1),
             "pending migration calculation removes applied ids");
  require_eq(pending.value().front().id, std::string_view("202605150902_second"),
             "pending migration calculation preserves deterministic order");

  fs::remove_all(root);
}

void migration_apply_tracks_success_and_stops_on_failure() {
  namespace fs = std::filesystem;

  const auto root = fs::temp_directory_path() / "eq2emu_source2_migration_apply_test";
  fs::remove_all(root);
  fs::create_directories(root);

  {
    std::ofstream(root / "202605150901_first.sql") << "select 1";
    std::ofstream(root / "202605150902_second.sql") << "select 2";
  }

  const auto migrations = eq2::db::discover_migrations(eq2::db::DatabaseKind::world, root);

  ScriptedConnection success_connection;
  eq2::db::MemoryAppliedMigrationStore success_store;
  auto success = eq2::db::apply_migrations(success_connection, success_store, migrations);
  require(success.has_value(), "migration apply succeeds through query executor");
  require_eq(success_connection.requests.size(), static_cast<std::size_t>(2),
             "migration apply executes each SQL file");

  auto success_pending = eq2::db::pending_migrations(migrations, success_store);
  require(success_pending.has_value(), "pending migration calculation works after apply");
  require(success_pending.value().empty(), "successful apply records all migrations");

  ScriptedConnection failing_connection;
  failing_connection.fail_on_sql_fragment = "select 2";
  eq2::db::MemoryAppliedMigrationStore failing_store;
  auto failure = eq2::db::apply_migrations(failing_connection, failing_store, migrations);
  require(!failure.has_value(), "migration apply reports failing SQL");
  require(failure.error().message.find("202605150902_second") != std::string::npos,
          "migration apply failure names the migration id");
  require_eq(failing_connection.requests.size(), static_cast<std::size_t>(2),
             "migration apply stops at the failed migration");

  auto first_applied = failing_store.is_applied(eq2::db::DatabaseKind::world, "202605150901_first");
  auto second_applied = failing_store.is_applied(eq2::db::DatabaseKind::world, "202605150902_second");
  require(first_applied.has_value() && first_applied.value(),
          "migration apply records migrations that completed before failure");
  require(second_applied.has_value() && !second_applied.value(),
          "migration apply does not record the failed migration");

  fs::remove_all(root);
}

void sql_migration_tracking_uses_schema_version_table() {
  auto connection = std::make_shared<ScriptedConnection>();
  auto ensured = eq2::db::ensure_migration_tracking_table(*connection);
  require(ensured.has_value(), "migration tracking table can be created through query executor");
  require(connection->requests.back().sql.find("source2_schema_version") != std::string::npos,
          "migration tracking table uses source2 schema version table");

  connection->results.push_back(eq2::db::QueryResult{
      .rows = {eq2::db::QueryRow{.columns = {{"migration_id", "202605150901_first"}}}},
  });

  eq2::db::SqlAppliedMigrationStore store(connection);
  auto applied = store.is_applied(eq2::db::DatabaseKind::login, "202605150901_first");
  require(applied.has_value() && applied.value(),
          "SQL migration tracking store detects applied migration rows");
  require_eq(connection->requests.back().parameters.front(), std::string_view("login"),
             "SQL migration tracking includes database name parameter");

  auto marked = store.mark_applied(eq2::db::DatabaseKind::world, "202605150902_second");
  require(marked.has_value(), "SQL migration tracking store records applied ids");
  require_eq(connection->requests.back().parameters.front(), std::string_view("world"),
             "SQL migration tracking records the target database");
  require_eq(connection->requests.back().parameters.back(), std::string_view("202605150902_second"),
             "SQL migration tracking records the migration id");
}

}  // namespace

int main() {
  database_config_captures_connection_settings();
  connection_pool_round_robins_connections();
  async_executor_runs_queries_off_the_owner_thread();
  fake_repositories_cover_critical_login_world_character_and_zone_boundaries();
  sql_repositories_execute_expected_query_boundaries();
  migration_discovery_orders_and_filters_pending_files();
  migration_apply_tracks_success_and_stops_on_failure();
  sql_migration_tracking_uses_schema_version_table();

  if (failures != 0) {
    std::cerr << failures << " db assertion(s) failed\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

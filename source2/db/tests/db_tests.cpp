#include <eq2/db/config.h>
#include <eq2/db/fake_database.h>
#include <eq2/db/migrations.h>
#include <eq2/db/query.h>
#include <eq2/db/repositories.h>
#include <eq2/db/sql_repositories.h>

#include <cstdint>
#include <cstdlib>
#include <array>
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
      .use_tls = true,
      .max_connections = 8,
  };

  require_eq(config.host, std::string_view("db.local"), "config stores host");
  require_eq(config.port, static_cast<std::uint16_t>(3307), "config stores port");
  require(config.use_tls, "config stores TLS preference");
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
  require_eq(characters.save_world_zone_updates(
                 77,
                 {
                     eq2::db::WorldZoneUpdateRecord{
                         .zone_id = 12,
                         .name = "qeynos",
                         .description = "Qeynos",
                     },
                 }),
             static_cast<std::size_t>(1),
             "fake character repository stores world zone updates");
  require_eq(characters.save_login_equipment_updates(
                 77,
                 {
                     eq2::db::LoginEquipmentUpdateRecord{
                         .update_id = 99,
                         .world_character_id = 2002,
                         .equip_type = 44,
                         .red = 1,
                         .green = 2,
                         .blue = 3,
                         .highlight_red = 4,
                         .highlight_green = 5,
                         .highlight_blue = 6,
                         .slot = 7,
                     },
                 }),
             static_cast<std::size_t>(1),
             "fake character repository stores login equipment updates");
  require_eq(characters.load_character_equipment(1001).front().equip_type, 44,
             "fake character repository indexes saved equipment by login character");
  const std::array<std::uint8_t, 3> picture{1, 2, 3};
  require(characters.save_character_picture(42, 2002, 77, picture),
          "fake character repository stores character pictures");
  require_eq(characters.saved_character_pictures.front().picture.size(),
             static_cast<std::size_t>(3),
             "fake character repository preserves picture bytes");

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
      .rows = {eq2::db::QueryRow{.columns = {{"version", "*"}}}},
  });
  eq2::db::SqlWorldRegistrationRepository worlds(connection);
  require(worlds.server_version_is_allowed("2026.05.14"),
          "SQL world repository accepts login_versions wildcard rows");
  require(connection->requests.back().sql.find("version = '*'") != std::string::npos,
          "SQL world repository checks legacy wildcard version");

  connection->results.push_back(eq2::db::QueryResult{
      .rows = {eq2::db::QueryRow{.columns = {{"id", "77"}}}},
  });
  require_eq(worlds.check_server_account("world-account", "password-hash"), 77,
             "SQL world repository maps world account id");
  require(connection->requests.back().sql.find("lower(password)") != std::string::npos,
          "SQL world repository accepts legacy hashed world password");
  require(connection->requests.back().sql.find("lower(sha2(?, 512))") != std::string::npos,
          "SQL world repository accepts raw world password without hash-case sensitivity");
  require_eq(connection->requests.back().parameters.size(), static_cast<std::size_t>(3),
             "SQL world repository parameterizes raw and hashed world password forms");

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
  require(connection->requests.back().sql.find("char_id as character_id") != std::string::npos,
          "SQL character repository reads the legacy char_id column");
  require(connection->requests.back().sql.find("deleted = 0") != std::string::npos,
          "SQL character repository filters deleted legacy characters");
  require_eq(list.front().name, std::string_view("Alys"),
             "SQL character repository maps character name");
  require_eq(list.front().current_zone_id, 10,
             "SQL character repository maps zone id");

  connection->results.push_back(eq2::db::QueryResult{
      .rows = {eq2::db::QueryRow{.columns = {{"appearance_id", "44"}}}},
  });
  connection->results.push_back(eq2::db::QueryResult{
      .rows = {eq2::db::QueryRow{.columns = {{"appearance_id", "88"}}}},
  });
  connection->results.push_back(eq2::db::QueryResult{});
  connection->results.push_back(eq2::db::QueryResult{
      .rows = {eq2::db::QueryRow{.columns = {{"id", "501"}}}},
  });
  const auto create_request_index = connection->requests.size();
  const auto saved = characters.save_created_character(eq2::db::CreatedCharacterRecord{
      .account_id = 42,
      .server_id = 77,
      .character_id = 3003,
      .name = "Newchar",
      .race = 1,
      .character_class = 4,
      .gender = 2,
      .deity = 3,
      .level = 1,
      .body_size = 0.75,
      .body_age = 0.25,
      .appearance_files =
          eq2::db::CreatedCharacterAppearanceFiles{
              .race_file = "model/human_male",
              .hair_file = "hair/short",
          },
      .appearance_values =
          {
              eq2::db::CharacterAppearanceRecord{
                  .type = "skin_color",
                  .signed_value = true,
                  .red = 25,
                  .green = 50,
                  .blue = 75,
              },
          },
  });
  require(saved, "SQL character repository saves created login character rows");
  require(connection->requests[create_request_index].sql.find("select appearance_id") !=
              std::string::npos,
          "SQL character repository resolves hair appearance ids by name");
  require(connection->requests[create_request_index + 1].sql.find("select appearance_id") !=
              std::string::npos,
          "SQL character repository resolves race appearance ids by name");
  require(connection->requests[create_request_index + 2].sql.find("insert into login_characters") !=
              std::string::npos,
          "SQL character repository inserts the legacy login character row");
  require_eq(connection->requests[create_request_index + 2].parameters[3],
             std::string_view("Newchar"),
             "SQL character repository parameterizes created character name");
  require_eq(connection->requests[create_request_index + 2].parameters[8],
             std::string_view("0.750000"),
             "SQL character repository saves created body size");
  require_eq(connection->requests[create_request_index + 2].parameters[18],
             std::string_view("44"),
             "SQL character repository stores resolved hair appearance id");
  require_eq(connection->requests[create_request_index + 2].parameters[21],
             std::string_view("88"),
             "SQL character repository stores resolved model appearance id");
  require(connection->requests[create_request_index + 3].sql.find("select id from login_characters") !=
              std::string::npos,
          "SQL character repository looks up the inserted login character id");
  require(connection->requests[create_request_index + 4].sql.find("deleted = 1") !=
              std::string::npos,
          "SQL character repository deactivates duplicate world character ids after create");
  require(connection->requests[create_request_index + 5].sql.find("insert into login_char_colors") !=
              std::string::npos,
          "SQL character repository saves created appearance color rows");
  require_eq(connection->requests[create_request_index + 5].parameters[0],
             std::string_view("501"),
             "SQL character repository stores colors against the inserted login character id");
  require_eq(connection->requests[create_request_index + 5].parameters[5],
             std::string_view("1"),
             "SQL character repository preserves signed legacy color rows");

  connection->results.push_back(eq2::db::QueryResult{.affected_rows = 1});
  require(characters.update_character_level(42, 3003, 77, 25),
          "SQL character repository updates level from world metadata");
  require(connection->requests.back().sql.find("set level") != std::string::npos,
          "SQL character repository issues legacy level update");

  connection->results.push_back(eq2::db::QueryResult{.affected_rows = 1});
  require(characters.update_character_name(42, 3003, 77, "Renamed"),
          "SQL character repository updates name from world metadata");
  require_eq(connection->requests.back().parameters.front(), std::string_view("Renamed"),
             "SQL character repository parameterizes renamed character");

  require_eq(characters.save_world_zone_updates(
                 77,
                 {
                     eq2::db::WorldZoneUpdateRecord{
                         .zone_id = 12,
                         .name = "qeynos",
                         .description = "Qeynos",
                     },
                 }),
             static_cast<std::size_t>(1),
             "SQL character repository stores world zone update rows");
  require(connection->requests.back().sql.find("replace into ls_world_zones") !=
              std::string::npos,
          "SQL character repository uses legacy world zone table");
  require_eq(connection->requests.back().parameters[2], std::string_view("qeynos"),
             "SQL character repository parameterizes world zone name");

  connection->results.push_back(eq2::db::QueryResult{
      .rows = {eq2::db::QueryRow{.columns = {{"id", "1001"}}}},
  });
  require_eq(characters.save_login_equipment_updates(
                 77,
                 {
                     eq2::db::LoginEquipmentUpdateRecord{
                         .update_id = 99,
                         .world_character_id = 2002,
                         .equip_type = 44,
                         .red = 1,
                         .green = 2,
                         .blue = 3,
                         .highlight_red = 4,
                         .highlight_green = 5,
                         .highlight_blue = 6,
                         .slot = 7,
                     },
                 }),
             static_cast<std::size_t>(1),
             "SQL character repository stores login equipment update rows");
  require(connection->requests[connection->requests.size() - 2].sql.find(
              "select id from login_characters") != std::string::npos,
          "SQL character repository maps world char id to login character id");
  require(connection->requests.back().sql.find("replace into login_equipment") !=
              std::string::npos,
          "SQL character repository uses legacy login equipment table");

  const std::array<std::uint8_t, 3> picture{0x0a, 0x0b, 0x0c};
  require(characters.save_character_picture(42, 2002, 77, picture),
          "SQL character repository stores character picture rows");
  require(connection->requests.back().sql.find("ls_character_picture") != std::string::npos,
          "SQL character repository uses legacy character picture table");
  require_eq(connection->requests.back().parameters[3], std::string_view("0a0b0c"),
             "SQL character repository hex-encodes picture bytes");

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

  eq2::db::MariaDbConnection mariadb(eq2::db::DatabaseConfig{.database = "eq2"});
  require_eq(mariadb.config().database, std::string_view("eq2"),
             "MariaDB adapter preserves database config");
#if !defined(EQ2_SOURCE2_HAS_MARIADB)
  const auto disabled_result = mariadb.execute(eq2::db::QueryRequest{.sql = "select 1"});
  require(!disabled_result.has_value(), "disabled MariaDB adapter reports unavailable explicitly");
  require_eq(disabled_result.error().code, eq2::core::ErrorCode::unavailable,
             "disabled MariaDB adapter uses unavailable error");
#endif
}

void login_opcode_lookup_reads_legacy_opcode_table() {
  ScriptedConnection connection;
  connection.results.push_back(eq2::db::QueryResult{
      .rows =
          {
              eq2::db::QueryRow{.columns = {{"name", "OP_LoginRequestMsg"}, {"opcode", "1"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_LoginReplyMsg"}, {"opcode", "2"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_WorldListMsg"}, {"opcode", "3"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_AllWSDescRequestMsg"}, {"opcode", "4"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_AllCharactersDescRequestMsg"}, {"opcode", "5"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_AllCharactersDescReplyMsg"}, {"opcode", "6"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_CreateCharacterRequestMsg"}, {"opcode", "7"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_CreateCharacterReplyMsg"}, {"opcode", "8"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_DeleteCharacterRequestMsg"}, {"opcode", "9"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_DeleteCharacterReplyMsg"}, {"opcode", "10"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_PlayCharacterRequestMsg"}, {"opcode", "11"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_PlayCharacterReplyMsg"}, {"opcode", "12"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_LsClientCrashlogReplyMsg"}, {"opcode", "13"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_LsClientEq2CrashLogReplyMsg"}, {"opcode", "14"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_LsClientAlertlogReplyMsg"}, {"opcode", "15"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_LsClientVerifylogReplyMsg"}, {"opcode", "16"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_LsClientBaselogReplyMsg"}, {"opcode", "17"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_WSLoginRequestMsg"}, {"opcode", "18"}}},
          },
  });

  const auto opcodes = eq2::db::load_login_opcode_set(connection, 546);

  require(opcodes.has_value(), "login opcode lookup succeeds from legacy opcodes table");
  if (opcodes.has_value()) {
    require_eq(opcodes.value().login_request_opcode, static_cast<std::uint16_t>(1),
               "login opcode lookup maps request opcode");
    require_eq(opcodes.value().login_reply_opcode, static_cast<std::uint16_t>(2),
               "login opcode lookup maps reply opcode");
    require_eq(opcodes.value().world_list_opcode, static_cast<std::uint16_t>(3),
               "login opcode lookup maps world-list opcode");
    require_eq(opcodes.value().all_worlds_request_opcode, static_cast<std::uint16_t>(4),
               "login opcode lookup maps all-worlds request opcode");
    require_eq(opcodes.value().characters_request_opcode, static_cast<std::uint16_t>(5),
               "login opcode lookup maps characters request opcode");
    require_eq(opcodes.value().characters_reply_opcode, static_cast<std::uint16_t>(6),
               "login opcode lookup maps characters reply opcode");
    require_eq(opcodes.value().create_character_request_opcode, static_cast<std::uint16_t>(7),
               "login opcode lookup maps create-character request opcode");
    require_eq(opcodes.value().create_character_reply_opcode, static_cast<std::uint16_t>(8),
               "login opcode lookup maps create-character reply opcode");
    require_eq(opcodes.value().delete_character_request_opcode, static_cast<std::uint16_t>(9),
               "login opcode lookup maps delete-character request opcode");
    require_eq(opcodes.value().delete_character_reply_opcode, static_cast<std::uint16_t>(10),
               "login opcode lookup maps delete-character reply opcode");
    require_eq(opcodes.value().play_character_request_opcode, static_cast<std::uint16_t>(11),
               "login opcode lookup maps play-character request opcode");
    require_eq(opcodes.value().play_character_reply_opcode, static_cast<std::uint16_t>(12),
               "login opcode lookup maps play-character reply opcode");
    require_eq(opcodes.value().client_crashlog_reply_opcode, static_cast<std::uint16_t>(13),
               "login opcode lookup maps client crashlog reply opcode");
    require_eq(opcodes.value().client_eq2_crashlog_reply_opcode, static_cast<std::uint16_t>(14),
               "login opcode lookup maps client eq2 crashlog reply opcode");
    require_eq(opcodes.value().client_alertlog_reply_opcode, static_cast<std::uint16_t>(15),
               "login opcode lookup maps client alertlog reply opcode");
    require_eq(opcodes.value().client_verifylog_reply_opcode, static_cast<std::uint16_t>(16),
               "login opcode lookup maps client verifylog reply opcode");
    require_eq(opcodes.value().client_baselog_reply_opcode, static_cast<std::uint16_t>(17),
               "login opcode lookup maps client baselog reply opcode");
    require_eq(opcodes.value().key_request_opcode, static_cast<std::uint16_t>(18),
               "login opcode lookup maps key request opcode");
  }
  require(connection.requests.front().sql.find("from opcodes") != std::string::npos,
          "login opcode lookup reads the opcodes table");
  require_eq(connection.requests.front().parameters.front(), std::string_view("546"),
             "login opcode lookup parameterizes client version");
}

void login_opcode_lookup_remaps_dof_client_log_reply_opcodes() {
  ScriptedConnection connection;
  connection.results.push_back(eq2::db::QueryResult{
      .rows =
          {
              eq2::db::QueryRow{.columns = {{"name", "OP_LoginRequestMsg"}, {"opcode", "0"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_LoginReplyMsg"}, {"opcode", "4"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_WorldListMsg"}, {"opcode", "8"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_AllWSDescRequestMsg"}, {"opcode", "9"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_AllCharactersDescRequestMsg"}, {"opcode", "10"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_AllCharactersDescReplyMsg"}, {"opcode", "11"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_CreateCharacterRequestMsg"}, {"opcode", "12"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_CreateCharacterReplyMsg"}, {"opcode", "13"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_DeleteCharacterRequestMsg"}, {"opcode", "16"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_DeleteCharacterReplyMsg"}, {"opcode", "17"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_PlayCharacterRequestMsg"}, {"opcode", "18"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_PlayCharacterReplyMsg"}, {"opcode", "19"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_LsClientBaselogReplyMsg"}, {"opcode", "213"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_LsClientCrashlogReplyMsg"}, {"opcode", "214"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_LsClientAlertlogReplyMsg"}, {"opcode", "215"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_LsClientVerifylogReplyMsg"}, {"opcode", "216"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_WSLoginRequestMsg"}, {"opcode", "2"}}},
          },
  });

  const auto opcodes = eq2::db::load_login_opcode_set(connection, 546);

  require(opcodes.has_value(), "login opcode lookup accepts packetparser DoF log rows");
  if (opcodes.has_value()) {
    require_eq(opcodes.value().client_baselog_reply_opcode, static_cast<std::uint16_t>(213),
               "DoF log remap keeps baselog opcode");
    require_eq(opcodes.value().client_crashlog_reply_opcode, static_cast<std::uint16_t>(214),
               "DoF log remap keeps crashlog opcode");
    require_eq(opcodes.value().client_eq2_crashlog_reply_opcode, static_cast<std::uint16_t>(215),
               "DoF log remap derives eq2 crashlog opcode");
    require_eq(opcodes.value().client_alertlog_reply_opcode, static_cast<std::uint16_t>(216),
               "DoF log remap shifts alertlog opcode");
    require_eq(opcodes.value().client_verifylog_reply_opcode, static_cast<std::uint16_t>(217),
               "DoF log remap derives verifylog opcode");
  }
}

void login_opcode_version_range_lookup_reads_matching_legacy_ranges() {
  ScriptedConnection connection;
  connection.results.push_back(eq2::db::QueryResult{
      .rows =
          {
              eq2::db::QueryRow{.columns = {{"version_range1", "546"}, {"version_range2", "561"}}},
          },
  });

  const auto ranges = eq2::db::load_login_opcode_version_ranges(connection, 550);

  require(ranges.has_value(), "login opcode version range lookup succeeds");
  if (ranges.has_value()) {
    require_eq(ranges.value().size(), static_cast<std::size_t>(1),
               "login opcode version range lookup maps matching row count");
    require_eq(ranges.value().front().min_version, static_cast<std::int16_t>(546),
               "login opcode version range lookup maps lower bound");
    require_eq(ranges.value().front().max_version, static_cast<std::int16_t>(561),
               "login opcode version range lookup maps upper bound");
  }
  require(connection.requests.front().sql.find("select distinct version_range1, version_range2 from opcodes") !=
              std::string::npos,
          "login opcode version range lookup reads the legacy opcode table ranges");
  require_eq(connection.requests.front().parameters.front(), std::string_view("550"),
             "login opcode version range lookup parameterizes client version");
}

void login_opcode_version_range_lookup_reports_missing_range() {
  ScriptedConnection connection;

  const auto ranges = eq2::db::load_login_opcode_version_ranges(connection, 999);

  require(!ranges.has_value(), "login opcode version range lookup fails when no range matches");
  require(ranges.error().message.find("999") != std::string::npos,
          "login opcode version range lookup names the missing client version");
}

void login_opcode_lookup_reports_missing_required_opcodes() {
  ScriptedConnection connection;
  connection.results.push_back(eq2::db::QueryResult{
      .rows =
          {
              eq2::db::QueryRow{.columns = {{"name", "OP_LoginRequestMsg"}, {"opcode", "1"}}},
              eq2::db::QueryRow{.columns = {{"name", "OP_LoginReplyMsg"}, {"opcode", "2"}}},
          },
  });

  const auto opcodes = eq2::db::load_login_opcode_set(connection, 546);

  require(!opcodes.has_value(), "login opcode lookup fails when world-list opcode is missing");
  require(opcodes.error().message.find("OP_WorldListMsg") != std::string::npos,
          "login opcode lookup names the missing opcode");
  require(opcodes.error().message.find("OP_AllWSDescRequestMsg") != std::string::npos,
          "login opcode lookup names the missing all-worlds request opcode");
  require(opcodes.error().message.find("OP_AllCharactersDescRequestMsg") != std::string::npos,
          "login opcode lookup names the missing characters request opcode");
  require(opcodes.error().message.find("OP_AllCharactersDescReplyMsg") != std::string::npos,
          "login opcode lookup names the missing characters reply opcode");
  require(opcodes.error().message.find("OP_CreateCharacterRequestMsg") != std::string::npos,
          "login opcode lookup names the missing create request opcode");
  require(opcodes.error().message.find("OP_PlayCharacterReplyMsg") != std::string::npos,
          "login opcode lookup names the missing play reply opcode");
  require(opcodes.error().message.find("OP_LsClientCrashlogReplyMsg") != std::string::npos,
          "login opcode lookup names the missing crashlog opcode");
  require(opcodes.error().message.find("OP_LsClientBaselogReplyMsg") != std::string::npos,
          "login opcode lookup names the missing baselog opcode");
  require(opcodes.error().message.find("OP_WSLoginRequestMsg") != std::string::npos,
          "login opcode lookup names the missing key request opcode");
}

void sql_client_log_repository_stores_legacy_log_messages() {
  auto connection = std::make_shared<ScriptedConnection>();
  eq2::db::SqlClientLogRepository logs(connection);
  logs.save_client_log(eq2::db::ClientLogRecord{
      .type = "Crash Log",
      .message = "client stack trace",
      .account_name = "tester",
      .client_version = 546,
  });

  require_eq(connection->requests.size(), static_cast<std::size_t>(1),
             "client log repository executes one insert");
  require(connection->requests.front().sql.find("insert into log_messages") != std::string::npos,
          "client log repository stores logs in the legacy log_messages table");
  require_eq(connection->requests.front().parameters[0], std::string_view("Crash Log"),
             "client log repository stores legacy log type");
  require_eq(connection->requests.front().parameters[1], std::string_view("client stack trace"),
             "client log repository stores decompressed message text");
  require_eq(connection->requests.front().parameters[2], std::string_view("tester"),
             "client log repository stores account name");
  require_eq(connection->requests.front().parameters[3], std::string_view("546"),
             "client log repository stores client version");
}

void sql_login_maintenance_repository_runs_legacy_cleanup_jobs() {
  auto connection = std::make_shared<ScriptedConnection>();
  eq2::db::SqlLoginMaintenanceRepository maintenance(connection);
  maintenance.remove_old_world_server_stats();
  maintenance.remove_deleted_character_data();
  maintenance.fix_bug_report_encoding();

  require_eq(connection->requests.size(), static_cast<std::size_t>(4),
             "login maintenance repository executes legacy cleanup statements");
  require(connection->requests[0].sql.find("delete from login_worldstats") != std::string::npos,
          "login maintenance removes old world stats");
  require(connection->requests[0].sql.find("86400") != std::string::npos,
          "login maintenance keeps legacy one-day world stats expiry");
  require(connection->requests[1].sql.find("delete from login_char_colors") != std::string::npos,
          "login maintenance removes deleted character color rows");
  require(connection->requests[2].sql.find("delete from login_equipment") != std::string::npos,
          "login maintenance removes deleted character equipment rows");
  require(connection->requests[3].sql.find("update bugs set description") != std::string::npos,
          "login maintenance runs legacy bug-report percent decoding fix");
}

void login_schema_preflight_checks_required_login_tables() {
  ScriptedConnection connection;
  const auto result = eq2::db::preflight_login_database_schema(connection);
  require(result.has_value(), "login schema preflight succeeds when probes execute");
  require_eq(connection.requests.size(), static_cast<std::size_t>(11),
             "login schema preflight checks tables and prepared account lookup");
  require(connection.requests[0].sql.find("from account") != std::string::npos,
          "login schema preflight checks account table");
  require(connection.requests[1].sql.find("from login_versions") != std::string::npos,
          "login schema preflight checks login_versions table");
  require(connection.requests[2].sql.find("from login_worldservers") != std::string::npos,
          "login schema preflight checks login_worldservers table");
  require(connection.requests[3].sql.find("from login_bannedips") != std::string::npos,
          "login schema preflight checks login_bannedips table");
  require(connection.requests[4].sql.find("from opcodes") != std::string::npos,
          "login schema preflight checks opcodes table");
  require(connection.requests[5].sql.find("from login_characters") != std::string::npos,
          "login schema preflight checks login_characters table");
  require(connection.requests[5].sql.find("char_id") != std::string::npos,
          "login schema preflight checks legacy login_characters char_id column");
  require(connection.requests[6].sql.find("from login_equipment") != std::string::npos,
          "login schema preflight checks login_equipment table");
  require(connection.requests[7].sql.find("from login_char_colors") != std::string::npos,
          "login schema preflight checks login_char_colors table");
  require(connection.requests[8].sql.find("from ls_world_zones") != std::string::npos,
          "login schema preflight checks ls_world_zones table");
  require(connection.requests[9].sql.find("from log_messages") != std::string::npos,
          "login schema preflight checks log_messages table");
  require(connection.requests[10].sql.find("name = ? and passwd = sha2(?, 512)") != std::string::npos,
          "login schema preflight checks parameterized account lookup");
  require_eq(connection.requests[10].parameters.size(), static_cast<std::size_t>(2),
             "login schema preflight exercises prepared statement parameters");
}

void login_schema_preflight_reports_failed_probe_context() {
  ScriptedConnection connection;
  connection.fail_on_sql_fragment = "login_worldservers";
  const auto result = eq2::db::preflight_login_database_schema(connection);
  require(!result.has_value(), "login schema preflight reports failed probes");
  require_eq(result.error().code, eq2::core::ErrorCode::parse_error,
             "login schema preflight preserves database error code");
  require(result.error().message.find("login_worldservers table") != std::string::npos,
          "login schema preflight failure names the failed table");
}

void login_schema_preflight_reports_prepared_lookup_failure() {
  ScriptedConnection connection;
  connection.fail_on_sql_fragment = "name = ? and passwd";
  const auto result = eq2::db::preflight_login_database_schema(connection);
  require(!result.has_value(), "login schema preflight reports prepared lookup failure");
  require(result.error().message.find("parameterized account lookup") != std::string::npos,
          "login schema preflight names prepared account lookup failure");
}

void world_schema_preflight_checks_required_world_tables() {
  ScriptedConnection connection;
  const auto result = eq2::db::preflight_world_database_schema(connection);
  require(result.has_value(), "world schema preflight succeeds when probes execute");
  require_eq(connection.requests.size(), static_cast<std::size_t>(2),
             "world schema preflight checks table and prepared lookup");
  require(connection.requests[0].sql.find("from zones") != std::string::npos,
          "world schema preflight checks zones table");
  require(connection.requests[1].sql.find("where id = ?") != std::string::npos,
          "world schema preflight checks parameterized zone lookup");
  require_eq(connection.requests[1].parameters.front(), std::string_view("0"),
             "world schema preflight exercises prepared statement parameters");
}

void world_schema_preflight_reports_failed_probe_context() {
  ScriptedConnection connection;
  connection.fail_on_sql_fragment = "from zones";
  const auto result = eq2::db::preflight_world_database_schema(connection);
  require(!result.has_value(), "world schema preflight reports failed probes");
  require(result.error().message.find("zones table") != std::string::npos,
          "world schema preflight failure names the failed table");
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
  login_opcode_lookup_reads_legacy_opcode_table();
  login_opcode_lookup_remaps_dof_client_log_reply_opcodes();
  login_opcode_version_range_lookup_reads_matching_legacy_ranges();
  login_opcode_version_range_lookup_reports_missing_range();
  login_opcode_lookup_reports_missing_required_opcodes();
  sql_client_log_repository_stores_legacy_log_messages();
  sql_login_maintenance_repository_runs_legacy_cleanup_jobs();
  login_schema_preflight_checks_required_login_tables();
  login_schema_preflight_reports_failed_probe_context();
  login_schema_preflight_reports_prepared_lookup_failure();
  world_schema_preflight_checks_required_world_tables();
  world_schema_preflight_reports_failed_probe_context();
  migration_discovery_orders_and_filters_pending_files();
  migration_apply_tracks_success_and_stops_on_failure();
  sql_migration_tracking_uses_schema_version_table();

  if (failures != 0) {
    std::cerr << failures << " db assertion(s) failed\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

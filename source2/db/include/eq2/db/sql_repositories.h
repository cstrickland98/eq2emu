#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <eq2/db/config.h>
#include <eq2/db/query.h>
#include <eq2/db/repositories.h>

namespace eq2::db {

namespace detail {

class MariaDbConnectionState;

inline auto to_i32(const std::optional<std::string>& value, std::int32_t fallback = 0) -> std::int32_t {
  if (!value.has_value()) {
    return fallback;
  }

  try {
    return static_cast<std::int32_t>(std::stol(*value));
  } catch (...) {
    return fallback;
  }
}

inline auto to_float(const std::optional<std::string>& value, float fallback = 0.0F) -> float {
  if (!value.has_value()) {
    return fallback;
  }

  try {
    return std::stof(*value);
  } catch (...) {
    return fallback;
  }
}

inline auto first_row(eq2::core::Result<QueryResult>& result) -> std::optional<QueryRow> {
  if (!result.has_value() || result.value().rows.empty()) {
    return std::nullopt;
  }

  return result.value().rows.front();
}

}  // namespace detail

struct LoginOpcodeSet {
  std::uint16_t login_request_opcode = 0;
  std::uint16_t login_reply_opcode = 0;
  std::uint16_t world_list_opcode = 0;
  std::uint16_t all_worlds_request_opcode = 0;
  std::uint16_t characters_request_opcode = 0;
  std::uint16_t characters_reply_opcode = 0;
  std::uint16_t key_request_opcode = 0;
};

struct OpcodeVersionRange {
  std::int16_t min_version = 0;
  std::int16_t max_version = 0;
};

inline auto load_login_opcode_version_ranges(QueryConnection& connection,
                                             std::int16_t client_version)
    -> eq2::core::Result<std::vector<OpcodeVersionRange>> {
  auto result = connection.execute(QueryRequest{
      .sql = "select distinct version_range1, version_range2 from opcodes "
             "where ? between version_range1 and version_range2 "
             "order by version_range1, version_range2",
      .parameters = {std::to_string(client_version)},
  });
  if (!result.has_value()) {
    return eq2::core::Result<std::vector<OpcodeVersionRange>>::failure(eq2::core::Error{
        .code = result.error().code,
        .message = "login opcode version range lookup failed: " + result.error().message,
    });
  }

  auto ranges = std::vector<OpcodeVersionRange>{};
  for (const auto& row : result.value().rows) {
    const auto min_version = detail::to_i32(row.get("version_range1"), -1);
    const auto max_version = detail::to_i32(row.get("version_range2"), -1);
    if (min_version < 0 || min_version > 0x7fff || max_version < 0 ||
        max_version > 0x7fff || min_version > max_version) {
      return eq2::core::Result<std::vector<OpcodeVersionRange>>::failure(eq2::core::Error{
          .code = eq2::core::ErrorCode::parse_error,
          .message = "login opcode version range lookup returned an invalid range",
      });
    }

    ranges.push_back(OpcodeVersionRange{
        .min_version = static_cast<std::int16_t>(min_version),
        .max_version = static_cast<std::int16_t>(max_version),
    });
  }

  if (ranges.empty()) {
    return eq2::core::Result<std::vector<OpcodeVersionRange>>::failure(eq2::core::Error{
        .code = eq2::core::ErrorCode::not_found,
        .message = "login opcode version range lookup found no range for client version " +
                   std::to_string(client_version),
    });
  }

  return eq2::core::Result<std::vector<OpcodeVersionRange>>::success(std::move(ranges));
}

inline auto load_login_opcode_set(QueryConnection& connection,
                                  std::int16_t client_version)
    -> eq2::core::Result<LoginOpcodeSet> {
  auto result = connection.execute(QueryRequest{
      .sql = "select name, opcode from opcodes "
             "where ? between version_range1 and version_range2 "
             "and name in ('OP_LoginRequestMsg', 'OP_LoginReplyMsg', 'OP_WorldListMsg', "
             "'OP_AllWSDescRequestMsg', 'OP_AllCharactersDescRequestMsg', "
             "'OP_AllCharactersDescReplyMsg', 'OP_WSLoginRequestMsg') "
             "order by version_range1, id",
      .parameters = {std::to_string(client_version)},
  });
  if (!result.has_value()) {
    return eq2::core::Result<LoginOpcodeSet>::failure(eq2::core::Error{
        .code = result.error().code,
        .message = "login opcode lookup failed: " + result.error().message,
    });
  }

  auto opcodes = LoginOpcodeSet{};
  auto has_request = false;
  auto has_reply = false;
  auto has_world_list = false;
  auto has_all_worlds_request = false;
  auto has_characters_request = false;
  auto has_characters_reply = false;
  auto has_key_request = false;

  for (const auto& row : result.value().rows) {
    const auto name = row.get("name").value_or("");
    const auto opcode_value = detail::to_i32(row.get("opcode"), -1);
    if (opcode_value < 0 || opcode_value > 0xffff) {
      return eq2::core::Result<LoginOpcodeSet>::failure(eq2::core::Error{
          .code = eq2::core::ErrorCode::parse_error,
          .message = "login opcode lookup returned an invalid opcode for " + name,
      });
    }

    const auto opcode = static_cast<std::uint16_t>(opcode_value);
    if (name == "OP_LoginRequestMsg") {
      opcodes.login_request_opcode = opcode;
      has_request = true;
    } else if (name == "OP_LoginReplyMsg") {
      opcodes.login_reply_opcode = opcode;
      has_reply = true;
    } else if (name == "OP_WorldListMsg") {
      opcodes.world_list_opcode = opcode;
      has_world_list = true;
    } else if (name == "OP_AllWSDescRequestMsg") {
      opcodes.all_worlds_request_opcode = opcode;
      has_all_worlds_request = true;
    } else if (name == "OP_AllCharactersDescRequestMsg") {
      opcodes.characters_request_opcode = opcode;
      has_characters_request = true;
    } else if (name == "OP_AllCharactersDescReplyMsg") {
      opcodes.characters_reply_opcode = opcode;
      has_characters_reply = true;
    } else if (name == "OP_WSLoginRequestMsg") {
      opcodes.key_request_opcode = opcode;
      has_key_request = true;
    }
  }

  auto missing = std::string{};
  if (!has_request) {
    missing += " OP_LoginRequestMsg";
  }
  if (!has_reply) {
    missing += " OP_LoginReplyMsg";
  }
  if (!has_world_list) {
    missing += " OP_WorldListMsg";
  }
  if (!has_all_worlds_request) {
    missing += " OP_AllWSDescRequestMsg";
  }
  if (!has_characters_request) {
    missing += " OP_AllCharactersDescRequestMsg";
  }
  if (!has_characters_reply) {
    missing += " OP_AllCharactersDescReplyMsg";
  }
  if (!has_key_request) {
    missing += " OP_WSLoginRequestMsg";
  }
  if (!missing.empty()) {
    return eq2::core::Result<LoginOpcodeSet>::failure(eq2::core::Error{
        .code = eq2::core::ErrorCode::not_found,
        .message = "login opcode lookup missing" + missing +
                   " for client version " + std::to_string(client_version),
    });
  }

  return eq2::core::Result<LoginOpcodeSet>::success(opcodes);
}

class SqlLoginAccountRepository final : public LoginAccountRepository {
 public:
  explicit SqlLoginAccountRepository(std::shared_ptr<QueryConnection> connection)
      : connection_(std::move(connection)) {}

  auto find_by_name_and_password(std::string_view username, std::string_view password)
      -> std::optional<LoginAccountRecord> override {
    auto result = connection_->execute(QueryRequest{
        .sql = "select id, name from account where name = ? and passwd = sha2(?, 512)",
        .parameters = {std::string(username), std::string(password)},
    });
    auto row = detail::first_row(result);
    if (!row.has_value()) {
      return std::nullopt;
    }

    return LoginAccountRecord{
        .id = detail::to_i32(row->get("id")),
        .name = row->get("name").value_or(std::string(username)),
    };
  }

  auto account_name_exists(std::string_view username) -> bool override {
    auto result = connection_->execute(QueryRequest{
        .sql = "select id from account where name = ?",
        .parameters = {std::string(username)},
    });
    return result.has_value() && !result.value().rows.empty();
  }

  auto create_account(std::string_view username, std::string_view password)
      -> LoginAccountRecord override {
    auto insert = connection_->execute(QueryRequest{
        .sql = "insert into account (name, passwd, created_date) "
               "values (?, sha2(?, 512), unix_timestamp())",
        .parameters = {std::string(username), std::string(password)},
    });
    if (!insert.has_value()) {
      return LoginAccountRecord{
          .id = 0,
          .name = std::string(username),
      };
    }

    if (auto row = detail::first_row(insert); row.has_value()) {
      return LoginAccountRecord{
          .id = detail::to_i32(row->get("id")),
          .name = row->get("name").value_or(std::string(username)),
      };
    }

    return find_by_name_and_password(username, password).value_or(LoginAccountRecord{
        .id = 0,
        .name = std::string(username),
    });
  }

 private:
  std::shared_ptr<QueryConnection> connection_;
};

class SqlWorldRegistrationRepository final : public WorldRegistrationRepository {
 public:
  explicit SqlWorldRegistrationRepository(std::shared_ptr<QueryConnection> connection)
      : connection_(std::move(connection)) {}

  auto server_version_is_allowed(std::string_view server_version) -> bool override {
    auto result = connection_->execute(QueryRequest{
        .sql = "select version from login_versions where version = ? or version = '*'",
        .parameters = {std::string(server_version)},
    });
    return result.has_value() && !result.value().rows.empty();
  }

  auto check_server_account(std::string_view account, std::string_view password)
      -> std::int32_t override {
    auto result = connection_->execute(QueryRequest{
        .sql = "select id from login_worldservers "
               "where account = ? and disabled = 0 "
               "and (lower(password) = lower(?) or lower(password) = lower(sha2(?, 512)))",
        .parameters = {std::string(account), std::string(password), std::string(password)},
    });
    auto row = detail::first_row(result);
    return row.has_value() ? detail::to_i32(row->get("id")) : 0;
  }

  auto account_is_disabled(std::string_view account) -> bool override {
    auto result = connection_->execute(QueryRequest{
        .sql = "select disabled from login_worldservers where account = ?",
        .parameters = {std::string(account)},
    });
    auto row = detail::first_row(result);
    return row.has_value() && detail::to_i32(row->get("disabled")) != 0;
  }

  auto connection_ip_is_banned() -> bool override {
    auto result = connection_->execute(QueryRequest{
        .sql = "select ip from login_bannedips where ip = ?",
        .parameters = {connection_ip_},
    });
    return result.has_value() && !result.value().rows.empty();
  }

  auto advertised_address_is_banned(std::string_view address) -> bool override {
    auto result = connection_->execute(QueryRequest{
        .sql = "select ip from login_bannedips where ip = ?",
        .parameters = {std::string(address)},
    });
    return result.has_value() && !result.value().rows.empty();
  }

  auto display_name_for_account(std::int32_t account_id) -> std::string override {
    auto result = connection_->execute(QueryRequest{
        .sql = "select name from login_worldservers where id = ?",
        .parameters = {std::to_string(account_id)},
    });
    auto row = detail::first_row(result);
    return row.has_value() ? row->get("name").value_or("") : "";
  }

  void set_connection_ip(std::string ip) {
    connection_ip_ = std::move(ip);
  }

 private:
  std::shared_ptr<QueryConnection> connection_;
  std::string connection_ip_;
};

class SqlCharacterListRepository final : public CharacterListRepository {
 public:
  explicit SqlCharacterListRepository(std::shared_ptr<QueryConnection> connection)
      : connection_(std::move(connection)) {}

  auto load_character_list(std::int32_t account_id) -> std::vector<CharacterListRecord> override {
    auto result = connection_->execute(QueryRequest{
        .sql = "select id, char_id as character_id, server_id, name, race, class, gender, "
               "current_zone_id, level from login_characters where account_id = ? and deleted = 0",
        .parameters = {std::to_string(account_id)},
    });
    if (!result.has_value()) {
      return {};
    }

    auto records = std::vector<CharacterListRecord>{};
    for (const auto& row : result.value().rows) {
      records.push_back(CharacterListRecord{
          .login_character_id = detail::to_i32(row.get("id")),
          .character_id = detail::to_i32(row.get("character_id")),
          .server_id = detail::to_i32(row.get("server_id")),
          .name = row.get("name").value_or(""),
          .race = detail::to_i32(row.get("race")),
          .character_class = detail::to_i32(row.get("class")),
          .gender = detail::to_i32(row.get("gender")),
          .current_zone_id = detail::to_i32(row.get("current_zone_id")),
          .level = detail::to_i32(row.get("level")),
      });
    }
    return records;
  }

 private:
  std::shared_ptr<QueryConnection> connection_;
};

class SqlZoneBootstrapRepository final : public ZoneBootstrapRepository {
 public:
  explicit SqlZoneBootstrapRepository(std::shared_ptr<QueryConnection> connection)
      : connection_(std::move(connection)) {}

  auto load_zone(std::int32_t zone_id) -> std::optional<ZoneBootstrapRecord> override {
    auto result = connection_->execute(QueryRequest{
        .sql = "select id, name, safe_x, safe_y, safe_z, safe_heading from zones where id = ?",
        .parameters = {std::to_string(zone_id)},
    });
    auto row = detail::first_row(result);
    if (!row.has_value()) {
      return std::nullopt;
    }

    return ZoneBootstrapRecord{
        .zone_id = detail::to_i32(row->get("id")),
        .name = row->get("name").value_or(""),
        .safe_x = detail::to_float(row->get("safe_x")),
        .safe_y = detail::to_float(row->get("safe_y")),
        .safe_z = detail::to_float(row->get("safe_z")),
        .safe_heading = detail::to_float(row->get("safe_heading")),
    };
  }

 private:
  std::shared_ptr<QueryConnection> connection_;
};

inline auto preflight_login_database_schema(QueryConnection& connection)
    -> eq2::core::Result<void> {
  struct SchemaProbe {
    std::string_view label;
    std::string sql;
  };

  const auto probes = std::vector<SchemaProbe>{
      SchemaProbe{
          .label = "account table",
          .sql = "select id, name, passwd, created_date from account where 1 = 0",
      },
      SchemaProbe{
          .label = "login_versions table",
          .sql = "select version from login_versions where 1 = 0",
      },
      SchemaProbe{
          .label = "login_worldservers table",
          .sql = "select id, account, password, disabled, name from login_worldservers where 1 = 0",
      },
      SchemaProbe{
          .label = "login_bannedips table",
          .sql = "select ip from login_bannedips where 1 = 0",
      },
      SchemaProbe{
          .label = "opcodes table",
          .sql = "select name, opcode, version_range1, version_range2 from opcodes where 1 = 0",
      },
      SchemaProbe{
          .label = "login_characters table",
          .sql = "select id, char_id, server_id, name, race, class, gender, current_zone_id, "
                 "level, deleted from login_characters where 1 = 0",
      },
      SchemaProbe{
          .label = "login_equipment table",
          .sql = "select login_characters_id, equip_type, red, green, blue, highlight_red, "
                 "highlight_green, highlight_blue, slot from login_equipment where 1 = 0",
      },
      SchemaProbe{
          .label = "login_char_colors table",
          .sql = "select login_characters_id, type, signed_value, red, green, blue "
                 "from login_char_colors where 1 = 0",
      },
      SchemaProbe{
          .label = "ls_world_zones table",
          .sql = "select server_id, zone_id, name, description from ls_world_zones where 1 = 0",
      },
  };

  for (const auto& probe : probes) {
    auto result = connection.execute(QueryRequest{.sql = probe.sql});
    if (!result.has_value()) {
      return eq2::core::Result<void>::failure(eq2::core::Error{
          .code = result.error().code,
          .message = "login database schema preflight failed for " +
                     std::string(probe.label) + ": " + result.error().message,
      });
    }
  }

  auto account_lookup = connection.execute(QueryRequest{
      .sql = "select id, name from account where name = ? and passwd = sha2(?, 512)",
      .parameters = {"__source2_preflight__", "__source2_preflight__"},
  });
  if (!account_lookup.has_value()) {
    return eq2::core::Result<void>::failure(eq2::core::Error{
        .code = account_lookup.error().code,
        .message = "login database schema preflight failed for parameterized account lookup: " +
                   account_lookup.error().message,
    });
  }

  return eq2::core::Result<void>::success();
}

inline auto preflight_world_database_schema(QueryConnection& connection)
    -> eq2::core::Result<void> {
  auto zones = connection.execute(QueryRequest{
      .sql = "select id, name, safe_x, safe_y, safe_z, safe_heading from zones where 1 = 0",
  });
  if (!zones.has_value()) {
    return eq2::core::Result<void>::failure(eq2::core::Error{
        .code = zones.error().code,
        .message = "world database schema preflight failed for zones table: " +
                   zones.error().message,
    });
  }

  auto zone_lookup = connection.execute(QueryRequest{
      .sql = "select id, name, safe_x, safe_y, safe_z, safe_heading from zones where id = ?",
      .parameters = {"0"},
  });
  if (!zone_lookup.has_value()) {
    return eq2::core::Result<void>::failure(eq2::core::Error{
        .code = zone_lookup.error().code,
        .message = "world database schema preflight failed for parameterized zone lookup: " +
                   zone_lookup.error().message,
    });
  }

  return eq2::core::Result<void>::success();
}

class MariaDbConnection final : public QueryConnection {
 public:
  explicit MariaDbConnection(DatabaseConfig config);
  MariaDbConnection(const MariaDbConnection&) = delete;
  auto operator=(const MariaDbConnection&) -> MariaDbConnection& = delete;
  MariaDbConnection(MariaDbConnection&&) = delete;
  auto operator=(MariaDbConnection&&) -> MariaDbConnection& = delete;
  ~MariaDbConnection() override;

  auto execute(const QueryRequest& request) -> eq2::core::Result<QueryResult> override;

  [[nodiscard]] auto config() const -> const DatabaseConfig&;
  [[nodiscard]] auto is_connected() const -> bool;

 private:
  DatabaseConfig config_;
  std::unique_ptr<detail::MariaDbConnectionState> state_;
  mutable std::mutex mutex_;
};

}  // namespace eq2::db

#pragma once

#include <cstdint>
#include <memory>
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
    auto result = connection_->execute(QueryRequest{
        .sql = "insert into account (name, passwd) values (?, sha2(?, 512)) returning id, name",
        .parameters = {std::string(username), std::string(password)},
    });
    auto row = detail::first_row(result);
    if (!row.has_value()) {
      return LoginAccountRecord{
          .id = 0,
          .name = std::string(username),
      };
    }

    return LoginAccountRecord{
        .id = detail::to_i32(row->get("id")),
        .name = row->get("name").value_or(std::string(username)),
    };
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
        .sql = "select version from login_versions where version = ?",
        .parameters = {std::string(server_version)},
    });
    return result.has_value() && !result.value().rows.empty();
  }

  auto check_server_account(std::string_view account, std::string_view password)
      -> std::int32_t override {
    auto result = connection_->execute(QueryRequest{
        .sql = "select id from login_worldservers where account = ? and password = sha2(?, 512)",
        .parameters = {std::string(account), std::string(password)},
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
        .sql = "select id, character_id, server_id, name, race, class, gender, current_zone_id, level "
               "from login_characters where account_id = ?",
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

class MariaDbConnection final : public QueryConnection {
 public:
  explicit MariaDbConnection(DatabaseConfig config) : config_(std::move(config)) {}

  auto execute(const QueryRequest&) -> eq2::core::Result<QueryResult> override {
    return eq2::core::Result<QueryResult>::failure(eq2::core::Error{
        .code = eq2::core::ErrorCode::unavailable,
        .message = "MariaDB C API adapter is not enabled in this source2 build",
    });
  }

  [[nodiscard]] auto config() const -> const DatabaseConfig& {
    return config_;
  }

 private:
  DatabaseConfig config_;
};

}  // namespace eq2::db

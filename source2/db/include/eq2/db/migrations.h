#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <eq2/db/query.h>

namespace eq2::db {

enum class DatabaseKind {
  login,
  world,
};

struct MigrationFile {
  DatabaseKind database = DatabaseKind::login;
  std::string id;
  std::filesystem::path path;
};

inline auto database_kind_name(DatabaseKind database) -> std::string_view {
  switch (database) {
    case DatabaseKind::login:
      return "login";
    case DatabaseKind::world:
      return "world";
  }

  return "unknown";
}

class AppliedMigrationStore {
 public:
  virtual ~AppliedMigrationStore() = default;

  virtual auto is_applied(DatabaseKind database, const std::string& id)
      -> eq2::core::Result<bool> = 0;
  virtual auto mark_applied(DatabaseKind database, const std::string& id)
      -> eq2::core::Result<void> = 0;
};

class MemoryAppliedMigrationStore final : public AppliedMigrationStore {
 public:
  auto is_applied(DatabaseKind database, const std::string& id)
      -> eq2::core::Result<bool> override {
    return eq2::core::Result<bool>::success(applied_.contains(key(database, id)));
  }

  auto mark_applied(DatabaseKind database, const std::string& id)
      -> eq2::core::Result<void> override {
    applied_.insert(key(database, id));
    return eq2::core::Result<void>::success();
  }

 private:
  static auto key(DatabaseKind database, const std::string& id) -> std::string {
    return std::to_string(static_cast<int>(database)) + ":" + id;
  }

  std::set<std::string> applied_;
};

class SqlAppliedMigrationStore final : public AppliedMigrationStore {
 public:
  explicit SqlAppliedMigrationStore(std::shared_ptr<QueryConnection> connection)
      : connection_(std::move(connection)) {}

  auto is_applied(DatabaseKind database, const std::string& id)
      -> eq2::core::Result<bool> override {
    if (!connection_) {
      return eq2::core::Result<bool>::failure(eq2::core::Error{
          .code = eq2::core::ErrorCode::unavailable,
          .message = "no database connection available for migration tracking",
      });
    }

    auto result = connection_->execute(QueryRequest{
        .sql =
            "select migration_id from source2_schema_version "
            "where database_name=? and migration_id=?",
        .parameters = {std::string(database_kind_name(database)), id},
    });
    if (!result.has_value()) {
      return eq2::core::Result<bool>::failure(result.error());
    }

    return eq2::core::Result<bool>::success(!result.value().rows.empty());
  }

  auto mark_applied(DatabaseKind database, const std::string& id)
      -> eq2::core::Result<void> override {
    if (!connection_) {
      return eq2::core::Result<void>::failure(eq2::core::Error{
          .code = eq2::core::ErrorCode::unavailable,
          .message = "no database connection available for migration tracking",
      });
    }

    auto result = connection_->execute(QueryRequest{
        .sql =
            "insert into source2_schema_version (database_name, migration_id) "
            "values (?, ?)",
        .parameters = {std::string(database_kind_name(database)), id},
    });
    if (!result.has_value()) {
      return eq2::core::Result<void>::failure(result.error());
    }

    return eq2::core::Result<void>::success();
  }

 private:
  std::shared_ptr<QueryConnection> connection_;
};

inline auto ensure_migration_tracking_table(QueryConnection& connection)
    -> eq2::core::Result<void> {
  auto result = connection.execute(QueryRequest{
      .sql =
          "create table if not exists source2_schema_version ("
          "database_name varchar(16) not null,"
          "migration_id varchar(128) not null,"
          "applied_at timestamp not null default current_timestamp,"
          "primary key (database_name, migration_id)"
          ")",
  });
  if (!result.has_value()) {
    return eq2::core::Result<void>::failure(result.error());
  }

  return eq2::core::Result<void>::success();
}

inline auto migration_id_from_path(const std::filesystem::path& path) -> std::string {
  return path.stem().string();
}

inline auto migration_id_is_valid(std::string_view id) -> bool {
  if (id.size() < 14 || id[12] != '_') {
    return false;
  }

  for (std::size_t i = 0; i < 12; ++i) {
    if (id[i] < '0' || id[i] > '9') {
      return false;
    }
  }

  return true;
}

inline auto discover_migrations(DatabaseKind database, const std::filesystem::path& directory)
    -> std::vector<MigrationFile> {
  auto migrations = std::vector<MigrationFile>{};
  if (!std::filesystem::exists(directory)) {
    return migrations;
  }

  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".sql") {
      continue;
    }

    migrations.push_back(MigrationFile{
        .database = database,
        .id = migration_id_from_path(entry.path()),
        .path = entry.path(),
    });
  }

  std::sort(migrations.begin(), migrations.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.id < rhs.id;
  });
  return migrations;
}

inline auto pending_migrations(const std::vector<MigrationFile>& discovered,
                               AppliedMigrationStore& applied)
    -> eq2::core::Result<std::vector<MigrationFile>> {
  auto pending = std::vector<MigrationFile>{};
  for (const auto& migration : discovered) {
    auto applied_result = applied.is_applied(migration.database, migration.id);
    if (!applied_result.has_value()) {
      return eq2::core::Result<std::vector<MigrationFile>>::failure(applied_result.error());
    }

    if (!applied_result.value()) {
      pending.push_back(migration);
    }
  }
  return eq2::core::Result<std::vector<MigrationFile>>::success(std::move(pending));
}

inline auto read_migration_sql(const MigrationFile& migration) -> eq2::core::Result<std::string> {
  std::ifstream input(migration.path);
  if (!input) {
    return eq2::core::Result<std::string>::failure(eq2::core::Error{
        .code = eq2::core::ErrorCode::not_found,
        .message = "migration file not found: " + migration.path.string(),
    });
  }

  return eq2::core::Result<std::string>::success(
      std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()));
}

inline auto apply_migration(QueryConnection& connection,
                            AppliedMigrationStore& applied,
                            const MigrationFile& migration) -> eq2::core::Result<void> {
  auto sql = read_migration_sql(migration);
  if (!sql.has_value()) {
    return eq2::core::Result<void>::failure(sql.error());
  }

  auto result = connection.execute(QueryRequest{
      .sql = sql.value(),
  });
  if (!result.has_value()) {
    return eq2::core::Result<void>::failure(result.error());
  }

  return applied.mark_applied(migration.database, migration.id);
}

inline auto apply_migrations(QueryConnection& connection,
                             AppliedMigrationStore& applied,
                             const std::vector<MigrationFile>& migrations)
    -> eq2::core::Result<void> {
  for (const auto& migration : migrations) {
    auto result = apply_migration(connection, applied, migration);
    if (!result.has_value()) {
      return eq2::core::Result<void>::failure(eq2::core::Error{
          .code = result.error().code,
          .message = "failed to apply migration " + migration.id + ": " + result.error().message,
      });
    }
  }

  return eq2::core::Result<void>::success();
}

inline auto record_baseline(AppliedMigrationStore& applied,
                            DatabaseKind database,
                            const std::string& baseline_id) -> eq2::core::Result<void> {
  return applied.mark_applied(database, baseline_id);
}

}  // namespace eq2::db

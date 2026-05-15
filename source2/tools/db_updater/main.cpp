#include <eq2/db/migrations.h>
#include <eq2/db/query.h>
#include <eq2/db/sql_repositories.h>

#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

enum class Command {
  list,
  dry_run,
  apply,
  record_baseline,
};

struct Options {
  eq2::db::DatabaseKind database = eq2::db::DatabaseKind::login;
  std::filesystem::path path;
  Command command = Command::list;
  eq2::db::DatabaseConfig db_config;
  std::vector<std::string> applied_ids;
  std::string baseline_id;
  bool use_mariadb = false;
  bool help = false;
};

class MemoryApplyConnection final : public eq2::db::QueryConnection {
 public:
  auto execute(const eq2::db::QueryRequest& request)
      -> eq2::core::Result<eq2::db::QueryResult> override {
    executed_sql.push_back(request.sql);
    return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{
        .affected_rows = 1,
    });
  }

  std::vector<std::string> executed_sql;
};

auto parse_database(std::string_view value) -> std::optional<eq2::db::DatabaseKind> {
  if (value == "login") {
    return eq2::db::DatabaseKind::login;
  }
  if (value == "world") {
    return eq2::db::DatabaseKind::world;
  }
  return std::nullopt;
}

auto default_updates_path(eq2::db::DatabaseKind database) -> std::filesystem::path {
  return std::filesystem::path("database") / std::string(eq2::db::database_kind_name(database)) /
         "updates";
}

auto parse_u16(std::string_view value) -> std::optional<std::uint16_t> {
  auto parsed = 0U;
  const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (ec != std::errc{} || ptr != value.data() + value.size() || parsed > 0xffffU) {
    return std::nullopt;
  }
  return static_cast<std::uint16_t>(parsed);
}

auto getenv_string(const char* name) -> std::optional<std::string> {
#if defined(_WIN32)
  char* raw_value = nullptr;
  std::size_t size = 0;
  if (_dupenv_s(&raw_value, &size, name) != 0 || raw_value == nullptr || size <= 1) {
    std::free(raw_value);
    return std::nullopt;
  }
  std::string value(raw_value);
  std::free(raw_value);
  return value;
#else
  const auto* value = std::getenv(name);
  if (value == nullptr || *value == '\0') {
    return std::nullopt;
  }
  return std::string(value);
#endif
}

auto env_or(std::string current, const char* name) -> std::string {
  if (!current.empty()) {
    return current;
  }
  return getenv_string(name).value_or("");
}

void apply_db_env_defaults(Options& options) {
  options.db_config.host = getenv_string("EQ2_DB_HOST").value_or(options.db_config.host);
  if (auto port = getenv_string("EQ2_DB_PORT")) {
    options.db_config.port = parse_u16(*port).value_or(options.db_config.port);
  }
  options.db_config.username = env_or(std::move(options.db_config.username), "EQ2_DB_USER");
  options.db_config.password = env_or(std::move(options.db_config.password), "EQ2_DB_PASSWORD");

  if (options.database == eq2::db::DatabaseKind::login) {
    options.db_config.database = env_or(std::move(options.db_config.database), "EQ2_LOGIN_DB_NAME");
  } else {
    options.db_config.database = env_or(std::move(options.db_config.database), "EQ2_WORLD_DB_NAME");
  }
  options.db_config.database = env_or(std::move(options.db_config.database), "EQ2_DB_NAME");
}

void print_help(std::ostream& output) {
  output << "Usage: eq2_db_updater [options]\n"
         << "\n"
         << "Options:\n"
         << "  --database login|world       Target database, defaults to login.\n"
         << "  --path <directory>           Migration directory, defaults to database/<db>/updates.\n"
         << "  --list                       List pending migrations. This is the default command.\n"
         << "  --dry-run                    Print pending migrations without applying them.\n"
         << "  --apply                      Apply pending migrations.\n"
         << "  --record-baseline <id>       Record an imported dump baseline in tracking state.\n"
         << "  --applied <id>               Pre-mark an id as applied for list/dry-run testing.\n"
         << "  --memory                     Use in-memory tracking/execution. This is the default.\n"
         << "  --mariadb                    Use MariaDB connection and schema-version tracking.\n"
         << "  --db-host <host>             MariaDB host, defaults to EQ2_DB_HOST or 127.0.0.1.\n"
         << "  --db-port <port>             MariaDB port, defaults to EQ2_DB_PORT or 3306.\n"
         << "  --db-name <name>             MariaDB schema name.\n"
         << "  --db-user <user>             MariaDB username, defaults to EQ2_DB_USER.\n"
         << "  --db-password <password>     MariaDB password, defaults to EQ2_DB_PASSWORD.\n"
         << "  --help                       Print this help text.\n";
}

auto parse_options(int argc, char** argv) -> eq2::core::Result<Options> {
  auto options = Options{};

  for (int i = 1; i < argc; ++i) {
    const auto arg = std::string_view(argv[i]);

    if (arg == "--help" || arg == "-h") {
      options.help = true;
    } else if (arg == "--database") {
      if (++i >= argc) {
        return eq2::core::Result<Options>::failure(eq2::core::Error{
            .code = eq2::core::ErrorCode::invalid_argument,
            .message = "--database requires login or world",
        });
      }

      auto database = parse_database(argv[i]);
      if (!database.has_value()) {
        return eq2::core::Result<Options>::failure(eq2::core::Error{
            .code = eq2::core::ErrorCode::invalid_argument,
            .message = "unsupported database: " + std::string(argv[i]),
        });
      }
      options.database = database.value();
    } else if (arg == "--path") {
      if (++i >= argc) {
        return eq2::core::Result<Options>::failure(eq2::core::Error{
            .code = eq2::core::ErrorCode::invalid_argument,
            .message = "--path requires a directory",
        });
      }
      options.path = argv[i];
    } else if (arg == "--list") {
      options.command = Command::list;
    } else if (arg == "--dry-run") {
      options.command = Command::dry_run;
    } else if (arg == "--apply") {
      options.command = Command::apply;
    } else if (arg == "--record-baseline") {
      if (++i >= argc) {
        return eq2::core::Result<Options>::failure(eq2::core::Error{
            .code = eq2::core::ErrorCode::invalid_argument,
            .message = "--record-baseline requires a migration id",
        });
      }
      options.command = Command::record_baseline;
      options.baseline_id = argv[i];
    } else if (arg == "--applied") {
      if (++i >= argc) {
        return eq2::core::Result<Options>::failure(eq2::core::Error{
            .code = eq2::core::ErrorCode::invalid_argument,
            .message = "--applied requires a migration id",
        });
      }
      options.applied_ids.emplace_back(argv[i]);
    } else if (arg == "--memory") {
      options.use_mariadb = false;
    } else if (arg == "--mariadb") {
      options.use_mariadb = true;
    } else if (arg == "--db-host") {
      if (++i >= argc) {
        return eq2::core::Result<Options>::failure(eq2::core::Error{
            .code = eq2::core::ErrorCode::invalid_argument,
            .message = "--db-host requires a host",
        });
      }
      options.db_config.host = argv[i];
    } else if (arg == "--db-port") {
      if (++i >= argc) {
        return eq2::core::Result<Options>::failure(eq2::core::Error{
            .code = eq2::core::ErrorCode::invalid_argument,
            .message = "--db-port requires a port",
        });
      }
      auto port = parse_u16(argv[i]);
      if (!port.has_value()) {
        return eq2::core::Result<Options>::failure(eq2::core::Error{
            .code = eq2::core::ErrorCode::invalid_argument,
            .message = "--db-port expected TCP port from 0 to 65535",
        });
      }
      options.db_config.port = *port;
    } else if (arg == "--db-name") {
      if (++i >= argc) {
        return eq2::core::Result<Options>::failure(eq2::core::Error{
            .code = eq2::core::ErrorCode::invalid_argument,
            .message = "--db-name requires a schema name",
        });
      }
      options.db_config.database = argv[i];
    } else if (arg == "--db-user") {
      if (++i >= argc) {
        return eq2::core::Result<Options>::failure(eq2::core::Error{
            .code = eq2::core::ErrorCode::invalid_argument,
            .message = "--db-user requires a username",
        });
      }
      options.db_config.username = argv[i];
    } else if (arg == "--db-password") {
      if (++i >= argc) {
        return eq2::core::Result<Options>::failure(eq2::core::Error{
            .code = eq2::core::ErrorCode::invalid_argument,
            .message = "--db-password requires a password",
        });
      }
      options.db_config.password = argv[i];
    } else {
      return eq2::core::Result<Options>::failure(eq2::core::Error{
          .code = eq2::core::ErrorCode::invalid_argument,
          .message = "unknown option: " + std::string(arg),
      });
    }
  }

  if (options.path.empty()) {
    options.path = default_updates_path(options.database);
  }
  apply_db_env_defaults(options);

  return eq2::core::Result<Options>::success(std::move(options));
}

auto load_pending(const Options& options,
                  eq2::db::AppliedMigrationStore& applied)
    -> eq2::core::Result<std::vector<eq2::db::MigrationFile>> {
  for (const auto& id : options.applied_ids) {
    auto marked = applied.mark_applied(options.database, id);
    if (!marked.has_value()) {
      return eq2::core::Result<std::vector<eq2::db::MigrationFile>>::failure(marked.error());
    }
  }

  const auto discovered = eq2::db::discover_migrations(options.database, options.path);
  return eq2::db::pending_migrations(discovered, applied);
}

void print_migrations(const std::vector<eq2::db::MigrationFile>& migrations,
                      std::string_view prefix) {
  if (migrations.empty()) {
    std::cout << prefix << ": none\n";
    return;
  }

  for (const auto& migration : migrations) {
    std::cout << prefix << ": " << migration.id << " " << migration.path.string() << '\n';
  }
}

auto run_memory_mode(const Options& options) -> int {
  eq2::db::MemoryAppliedMigrationStore applied;
  if (options.command == Command::record_baseline) {
    auto result = eq2::db::record_baseline(applied, options.database, options.baseline_id);
    if (!result.has_value()) {
      std::cerr << "error: " << result.error().message << '\n';
      return EXIT_FAILURE;
    }

    std::cout << "recorded baseline: " << eq2::db::database_kind_name(options.database) << " "
              << options.baseline_id << '\n';
    return EXIT_SUCCESS;
  }

  auto pending = load_pending(options, applied);
  if (!pending.has_value()) {
    std::cerr << "error: " << pending.error().message << '\n';
    return EXIT_FAILURE;
  }

  if (options.command == Command::list) {
    print_migrations(pending.value(), "pending");
    return EXIT_SUCCESS;
  }

  if (options.command == Command::dry_run) {
    print_migrations(pending.value(), "would apply");
    return EXIT_SUCCESS;
  }

  MemoryApplyConnection connection;
  auto applied_result = eq2::db::apply_migrations(connection, applied, pending.value());
  if (!applied_result.has_value()) {
    std::cerr << "error: " << applied_result.error().message << '\n';
    return EXIT_FAILURE;
  }

  std::cout << "applied: " << connection.executed_sql.size() << '\n';
  return EXIT_SUCCESS;
}

auto validate_mariadb_options(const Options& options) -> std::optional<std::string> {
  if (options.db_config.database.empty()) {
    return "--mariadb requires --db-name, EQ2_DB_NAME, EQ2_LOGIN_DB_NAME, or EQ2_WORLD_DB_NAME";
  }
  if (options.db_config.username.empty()) {
    return "--mariadb requires --db-user or EQ2_DB_USER";
  }
  return std::nullopt;
}

auto run_mariadb_mode(const Options& options) -> int {
  if (auto error = validate_mariadb_options(options)) {
    std::cerr << "error: " << *error << '\n';
    return EXIT_FAILURE;
  }

  auto connection = std::make_shared<eq2::db::MariaDbConnection>(options.db_config);
  auto tracking_ready = eq2::db::ensure_migration_tracking_table(*connection);
  if (!tracking_ready.has_value()) {
    std::cerr << "error: " << tracking_ready.error().message << '\n';
    return EXIT_FAILURE;
  }

  eq2::db::SqlAppliedMigrationStore applied(connection);
  if (options.command == Command::record_baseline) {
    auto result = eq2::db::record_baseline(applied, options.database, options.baseline_id);
    if (!result.has_value()) {
      std::cerr << "error: " << result.error().message << '\n';
      return EXIT_FAILURE;
    }

    std::cout << "recorded baseline: " << eq2::db::database_kind_name(options.database) << " "
              << options.baseline_id << '\n';
    return EXIT_SUCCESS;
  }

  auto pending = load_pending(options, applied);
  if (!pending.has_value()) {
    std::cerr << "error: " << pending.error().message << '\n';
    return EXIT_FAILURE;
  }

  if (options.command == Command::list) {
    print_migrations(pending.value(), "pending");
    return EXIT_SUCCESS;
  }

  if (options.command == Command::dry_run) {
    print_migrations(pending.value(), "would apply");
    return EXIT_SUCCESS;
  }

  auto result = eq2::db::apply_migrations(*connection, applied, pending.value());
  if (!result.has_value()) {
    std::cerr << "error: " << result.error().message << '\n';
    return EXIT_FAILURE;
  }

  std::cout << "applied: " << pending.value().size() << '\n';
  return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char** argv) {
  auto parsed = parse_options(argc, argv);
  if (!parsed.has_value()) {
    std::cerr << "error: " << parsed.error().message << '\n';
    print_help(std::cerr);
    return EXIT_FAILURE;
  }

  auto options = std::move(parsed.value());
  if (options.help) {
    print_help(std::cout);
    return EXIT_SUCCESS;
  }

  return options.use_mariadb ? run_mariadb_mode(options) : run_memory_mode(options);
}

#include <eq2/db/migrations.h>
#include <eq2/db/query.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
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
  std::vector<std::string> applied_ids;
  std::string baseline_id;
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

void print_help(std::ostream& output) {
  output << "Usage: eq2_db_updater [options]\n"
         << "\n"
         << "Options:\n"
         << "  --database login|world       Target database, defaults to login.\n"
         << "  --path <directory>           Migration directory, defaults to database/<db>/updates.\n"
         << "  --list                       List pending migrations. This is the default command.\n"
         << "  --dry-run                    Print pending migrations without applying them.\n"
         << "  --apply                      Apply pending migrations through the in-memory executor.\n"
         << "  --record-baseline <id>       Record an imported dump baseline in tracking state.\n"
         << "  --applied <id>               Pre-mark an id as applied for list/dry-run testing.\n"
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

  return eq2::core::Result<Options>::success(std::move(options));
}

auto load_pending(const Options& options,
                  eq2::db::MemoryAppliedMigrationStore& applied)
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

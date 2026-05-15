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

#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace {

std::atomic_bool stop_requested = false;

void request_stop(int) {
  stop_requested = true;
}

class SmokeLoginWorldConnection final : public eq2::db::QueryConnection {
 public:
  auto execute(const eq2::db::QueryRequest& request)
      -> eq2::core::Result<eq2::db::QueryResult> override {
    if (request.sql.find("login_versions") != std::string::npos) {
      return rows({eq2::db::QueryRow{.columns = {{"version", "2026.05.14"}}}});
    }
    if (request.sql.find("from login_worldservers") != std::string::npos &&
        request.sql.find("lower(password)") != std::string::npos) {
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

enum class Command {
  validate_config,
  serve,
  check_world_db,
  smoke_world_live,
  help,
};

struct AppOptions {
  Command command = Command::validate_config;
  std::optional<std::string> config_path;
  std::vector<std::pair<std::string, std::string>> overrides;
  std::optional<std::uint32_t> run_for_ms;
};

auto parse_u16(std::string_view value) -> std::optional<std::uint16_t> {
  auto parsed = 0U;
  const auto [ptr, error] =
      std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (error != std::errc{} || ptr != value.data() + value.size() || parsed > 0xffffU) {
    return std::nullopt;
  }

  return static_cast<std::uint16_t>(parsed);
}

auto parse_u32(std::string_view value) -> std::optional<std::uint32_t> {
  auto parsed = std::uint32_t{0};
  const auto [ptr, error] =
      std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (error != std::errc{} || ptr != value.data() + value.size()) {
    return std::nullopt;
  }

  return parsed;
}

auto parse_bool(std::string_view value) -> std::optional<bool> {
  if (value == "1" || value == "true" || value == "yes" || value == "on") {
    return true;
  }
  if (value == "0" || value == "false" || value == "no" || value == "off") {
    return false;
  }
  return std::nullopt;
}

auto getenv_string(const char* name) -> std::optional<std::string> {
#ifdef _WIN32
  char* value = nullptr;
  std::size_t size = 0;
  if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
    return std::nullopt;
  }

  std::string result(value);
  free(value);
  return result;
#else
  const auto* value = std::getenv(name);
  if (value == nullptr) {
    return std::nullopt;
  }

  return std::string(value);
#endif
}

void apply_env(eq2::core::MapConfig& config, const char* name, std::string key) {
  if (auto value = getenv_string(name)) {
    config.set(std::move(key), std::move(*value));
  }
}

void apply_environment_config(eq2::core::MapConfig& config) {
  apply_env(config, "EQ2_WORLD_ADDRESS", "world.address");
  apply_env(config, "EQ2_WORLD_PORT", "world.port");
  apply_env(config, "EQ2_WORLD_ADVERTISED_ADDRESS", "world.advertised_address");
  apply_env(config, "EQ2_WORLD_NAME", "world.name");
  apply_env(config, "EQ2_WORLD_ACCOUNT", "world.account");
  apply_env(config, "EQ2_WORLD_PASSWORD", "world.password");
  apply_env(config, "EQ2_WORLD_PROTOCOL_VERSION", "world.protocol_version");
  apply_env(config, "EQ2_WORLD_SERVER_VERSION", "world.server_version");
  apply_env(config, "EQ2_WORLD_DATABASE_VERSION", "world.database_version");
  apply_env(config, "EQ2_LOGIN_ADDRESS", "login.remote_address");
  apply_env(config, "EQ2_LOGIN_PORT", "login.remote_port");

  apply_env(config, "EQ2_DB_HOST", "world_db.host");
  apply_env(config, "EQ2_DB_PORT", "world_db.port");
  apply_env(config, "EQ2_DB_USER", "world_db.user");
  apply_env(config, "EQ2_DB_PASSWORD", "world_db.password");
  apply_env(config, "EQ2_DB_TLS", "world_db.tls");
  apply_env(config, "EQ2_WORLD_DB_NAME", "world_db.name");
  if (!config.get("world_db.name").has_value()) {
    apply_env(config, "EQ2_DB_NAME", "world_db.name");
  }
}

void apply_overrides(eq2::core::MapConfig& config, const AppOptions& options) {
  for (const auto& [key, value] : options.overrides) {
    config.set(key, value);
  }
}

void print_help() {
  std::cout
      << "Usage: eq2_world_server [options]\n"
      << "\n"
      << "Commands:\n"
      << "  --serve                     Start source2 world and register with login.\n"
      << "  --check-world-db            Check MariaDB world database connectivity/schema.\n"
      << "  --smoke-world-live          Run the in-memory loopback smoke test used by CI.\n"
      << "  --validate-config           Validate config and exit (default).\n"
      << "  --help                      Show this help.\n"
      << "\n"
      << "Config:\n"
      << "  Precedence: INI config, environment variables, then CLI overrides.\n"
      << "  --config <path>             Load INI config. Sections become key prefixes.\n"
      << "  --set <key=value>           Override any config key.\n"
      << "  --world-address <address>   World listen address.\n"
      << "  --world-port <port>         World listen port.\n"
      << "  --world-advertised-address <address> World address sent to login clients.\n"
      << "  --world-name <name>         World display name advertised to login.\n"
      << "  --world-account <account>   Login DB world-server account.\n"
      << "  --world-password <password> Login DB world-server password.\n"
      << "  --world-protocol-version <version> World protocol version string.\n"
      << "  --world-server-version <version> World server version allowed by login.\n"
      << "  --world-database-version <version> World DB version advertised to login.\n"
      << "  --login-address <address>   Login server address used by world registration.\n"
      << "  --login-port <port>         Login server port used by world registration.\n"
      << "  --db-host <host>            MariaDB world DB host.\n"
      << "  --db-port <port>            MariaDB world DB port.\n"
      << "  --db-name <name>            MariaDB world database name.\n"
      << "  --db-user <user>            MariaDB username.\n"
      << "  --db-password <password>    MariaDB password.\n"
      << "  --db-tls <bool>             Enable MariaDB TLS, default false.\n"
      << "  --run-for-ms <ms>           Stop --serve automatically after this many milliseconds.\n"
      << "\n"
      << "Environment aliases:\n"
      << "  EQ2_WORLD_ADDRESS, EQ2_WORLD_PORT, EQ2_WORLD_ADVERTISED_ADDRESS,\n"
      << "  EQ2_WORLD_NAME, EQ2_WORLD_ACCOUNT, EQ2_WORLD_PASSWORD,\n"
      << "  EQ2_WORLD_PROTOCOL_VERSION, EQ2_WORLD_SERVER_VERSION,\n"
      << "  EQ2_WORLD_DATABASE_VERSION,\n"
      << "  EQ2_LOGIN_ADDRESS, EQ2_LOGIN_PORT,\n"
      << "  EQ2_DB_HOST, EQ2_DB_PORT, EQ2_WORLD_DB_NAME, EQ2_DB_NAME,\n"
      << "  EQ2_DB_USER, EQ2_DB_PASSWORD, EQ2_DB_TLS.\n";
}

auto add_key_value_override(AppOptions& options,
                            std::string key,
                            std::string value) -> bool {
  options.overrides.emplace_back(std::move(key), std::move(value));
  return true;
}

auto parse_arguments(int argc, char** argv) -> eq2::core::Result<AppOptions> {
  AppOptions options;
  for (auto index = 1; index < argc; ++index) {
    const auto arg = std::string_view(argv[index]);
    const auto require_value = [&](std::string_view option) -> std::optional<std::string> {
      if (index + 1 >= argc) {
        (void)option;
        return std::nullopt;
      }
      ++index;
      return std::string(argv[index]);
    };

    if (arg == "--help" || arg == "-h") {
      options.command = Command::help;
    } else if (arg == "--validate-config") {
      options.command = Command::validate_config;
    } else if (arg == "--serve") {
      options.command = Command::serve;
    } else if (arg == "--check-world-db") {
      options.command = Command::check_world_db;
    } else if (arg == "--smoke-world-live") {
      options.command = Command::smoke_world_live;
    } else if (arg == "--strict-config") {
      add_key_value_override(options, "world.port", "invalid");
    } else if (arg == "--config") {
      auto value = require_value(arg);
      if (!value.has_value()) {
        return eq2::core::Result<AppOptions>::failure({.message = "--config requires a path"});
      }
      options.config_path = std::move(*value);
    } else if (arg == "--set") {
      auto value = require_value(arg);
      if (!value.has_value()) {
        return eq2::core::Result<AppOptions>::failure({.message = "--set requires key=value"});
      }
      const auto separator = value->find('=');
      if (separator == std::string::npos) {
        return eq2::core::Result<AppOptions>::failure({.message = "--set requires key=value"});
      }
      add_key_value_override(options, value->substr(0, separator), value->substr(separator + 1));
    } else if (arg == "--world-address") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--world-address requires a value"});
      }
      add_key_value_override(options, "world.address", std::move(*value));
    } else if (arg == "--world-port") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--world-port requires a value"});
      }
      add_key_value_override(options, "world.port", std::move(*value));
    } else if (arg == "--world-advertised-address") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--world-advertised-address requires a value"});
      }
      add_key_value_override(options, "world.advertised_address", std::move(*value));
    } else if (arg == "--world-name") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--world-name requires a value"});
      }
      add_key_value_override(options, "world.name", std::move(*value));
    } else if (arg == "--world-account") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--world-account requires a value"});
      }
      add_key_value_override(options, "world.account", std::move(*value));
    } else if (arg == "--world-password") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--world-password requires a value"});
      }
      add_key_value_override(options, "world.password", std::move(*value));
    } else if (arg == "--world-protocol-version") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--world-protocol-version requires a value"});
      }
      add_key_value_override(options, "world.protocol_version", std::move(*value));
    } else if (arg == "--world-server-version") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--world-server-version requires a value"});
      }
      add_key_value_override(options, "world.server_version", std::move(*value));
    } else if (arg == "--world-database-version") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--world-database-version requires a value"});
      }
      if (!parse_u32(*value).has_value()) {
        return eq2::core::Result<AppOptions>::failure({.message = "--world-database-version requires an integer"});
      }
      add_key_value_override(options, "world.database_version", std::move(*value));
    } else if (arg == "--login-address") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--login-address requires a value"});
      }
      add_key_value_override(options, "login.remote_address", std::move(*value));
    } else if (arg == "--login-port") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--login-port requires a value"});
      }
      add_key_value_override(options, "login.remote_port", std::move(*value));
    } else if (arg == "--db-host") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--db-host requires a value"});
      }
      add_key_value_override(options, "world_db.host", std::move(*value));
    } else if (arg == "--db-port") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--db-port requires a value"});
      }
      add_key_value_override(options, "world_db.port", std::move(*value));
    } else if (arg == "--db-name") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--db-name requires a value"});
      }
      add_key_value_override(options, "world_db.name", std::move(*value));
    } else if (arg == "--db-user") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--db-user requires a value"});
      }
      add_key_value_override(options, "world_db.user", std::move(*value));
    } else if (arg == "--db-password") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--db-password requires a value"});
      }
      add_key_value_override(options, "world_db.password", std::move(*value));
    } else if (arg == "--db-tls") {
      auto value = require_value(arg);
      if (!value || !parse_bool(*value).has_value()) {
        return eq2::core::Result<AppOptions>::failure({.message = "--db-tls requires a boolean"});
      }
      add_key_value_override(options, "world_db.tls", std::move(*value));
    } else if (arg == "--run-for-ms") {
      auto value = require_value(arg);
      if (!value || !parse_u32(*value).has_value()) {
        return eq2::core::Result<AppOptions>::failure({.message = "--run-for-ms requires an integer"});
      }
      options.run_for_ms = *parse_u32(*value);
    } else {
      return eq2::core::Result<AppOptions>::failure({
          .code = eq2::core::ErrorCode::invalid_argument,
          .message = "unknown option: " + std::string(arg),
      });
    }
  }

  return eq2::core::Result<AppOptions>::success(std::move(options));
}

auto load_app_config(const AppOptions& options) -> eq2::core::Result<eq2::core::MapConfig> {
  eq2::core::MapConfig config;
  if (options.config_path.has_value()) {
    auto loaded = eq2::core::load_ini_config_file(*options.config_path);
    if (!loaded.has_value()) {
      return loaded;
    }
    config = std::move(loaded.value());
  }

  apply_environment_config(config);
  apply_overrides(config, options);
  return eq2::core::Result<eq2::core::MapConfig>::success(std::move(config));
}

auto validate_world_registration_config(const eq2::world::WorldServerConfig& config)
    -> std::optional<std::string> {
  if (config.world_name.empty()) {
    return "world name is required; use --world-name, world.name, or EQ2_WORLD_NAME";
  }
  if (config.world_account.empty()) {
    return "world login account is required; use --world-account, world.account, or EQ2_WORLD_ACCOUNT";
  }
  if (config.world_password.empty()) {
    return "world login password is required; use --world-password, world.password, or EQ2_WORLD_PASSWORD";
  }
  if (config.server_version.empty()) {
    return "world server version is required; use --world-server-version, world.server_version, or EQ2_WORLD_SERVER_VERSION";
  }
  return std::nullopt;
}

auto load_world_database_config(const eq2::core::ConfigProvider& config)
    -> eq2::db::DatabaseConfig {
  return eq2::db::DatabaseConfig{
      .host = config.get("world_db.host").value_or(config.get("db.host").value_or("127.0.0.1")),
      .port = parse_u16(config.get("world_db.port").value_or(config.get("db.port").value_or("3306"))).value_or(3306),
      .database = config.get("world_db.name").value_or(config.get("db.name").value_or("")),
      .username = config.get("world_db.user").value_or(config.get("db.user").value_or("")),
      .password = config.get("world_db.password").value_or(config.get("db.password").value_or("")),
      .use_tls = parse_bool(config.get("world_db.tls").value_or(config.get("db.tls").value_or("false"))).value_or(false),
  };
}

auto validate_world_database_config(const eq2::core::ConfigProvider& provider,
                                    const eq2::db::DatabaseConfig& config)
    -> std::optional<std::string> {
  if (auto port = provider.get("world_db.port"); port.has_value() && !parse_u16(*port).has_value()) {
    return "MariaDB world DB port must be an integer from 0 to 65535";
  }
  if (auto tls = provider.get("world_db.tls"); tls.has_value() && !parse_bool(*tls).has_value()) {
    return "MariaDB world DB TLS setting must be a boolean";
  }
  if (auto tls = provider.get("db.tls"); tls.has_value() && !parse_bool(*tls).has_value()) {
    return "MariaDB world DB TLS setting must be a boolean";
  }
  if (config.database.empty()) {
    return "MariaDB world database is required; use --db-name, world_db.name, EQ2_WORLD_DB_NAME, or EQ2_DB_NAME";
  }
  if (config.username.empty()) {
    return "MariaDB user is required; use --db-user, world_db.user, or EQ2_DB_USER";
  }
  return std::nullopt;
}

auto run_world_db_check(const eq2::core::ConfigProvider& config) -> int {
  auto db_config = load_world_database_config(config);
  if (auto error = validate_world_database_config(config, db_config)) {
    std::cerr << *error << '\n';
    return EXIT_FAILURE;
  }

  auto connection = std::make_shared<eq2::db::MariaDbConnection>(std::move(db_config));
  auto preflight = connection->execute(eq2::db::QueryRequest{.sql = "select 1"});
  if (!preflight.has_value()) {
    std::cerr << preflight.error().message << '\n';
    return EXIT_FAILURE;
  }
  auto schema = eq2::db::preflight_world_database_schema(*connection);
  if (!schema.has_value()) {
    std::cerr << schema.error().message << '\n';
    return EXIT_FAILURE;
  }

  std::cout << "world-db-check ok database=" << connection->config().database << '\n';
  return EXIT_SUCCESS;
}

auto run_server(const eq2::core::ConfigProvider& config, const AppOptions& options) -> int {
  auto runtime = eq2::core::load_runtime_config(config);
  if (!runtime.has_value()) {
    std::cerr << runtime.error().message << '\n';
    return EXIT_FAILURE;
  }

  auto world_config = eq2::world::load_world_server_config(config);
  if (auto error = validate_world_registration_config(world_config)) {
    std::cerr << *error << '\n';
    return EXIT_FAILURE;
  }

  auto db_config = load_world_database_config(config);
  if (auto error = validate_world_database_config(config, db_config)) {
    std::cerr << *error << '\n';
    return EXIT_FAILURE;
  }

  auto connection = std::make_shared<eq2::db::MariaDbConnection>(std::move(db_config));
  auto preflight = connection->execute(eq2::db::QueryRequest{.sql = "select 1"});
  if (!preflight.has_value()) {
    std::cerr << preflight.error().message << '\n';
    return EXIT_FAILURE;
  }
  auto schema = eq2::db::preflight_world_database_schema(*connection);
  if (!schema.has_value()) {
    std::cerr << schema.error().message << '\n';
    return EXIT_FAILURE;
  }

  eq2::db::SqlCharacterListRepository characters(connection);
  eq2::db::SqlZoneBootstrapRepository zone_repository(connection);
  eq2::zone::ZoneBootstrapService zones(zone_repository);
  eq2::world::ZoneRuntimeHandoff zone_handoff(zones);
  eq2::world::LiveWorldService service(world_config, characters, zone_handoff);

  if (!service.start()) {
    std::cerr << "failed to start source2 world server on "
              << world_config.address << ':' << world_config.port << '\n';
    return EXIT_FAILURE;
  }
  if (!service.register_with_login()) {
    service.stop();
    std::cerr << "failed to register source2 world with login at "
              << world_config.login_address << ':' << world_config.login_port << '\n';
    return EXIT_FAILURE;
  }

  std::signal(SIGINT, request_stop);
  std::signal(SIGTERM, request_stop);

  eq2::core::ConsoleLogSink log;
  eq2::core::log(log, eq2::core::LogLevel::info, "world", "source2 world server started");
  std::cout << "eq2_world_server source2 serving TCP "
            << world_config.address << ':' << service.port()
            << " login=" << world_config.login_address << ':' << world_config.login_port
            << " world=\"" << world_config.world_name << "\""
            << " version=" << eq2::core::kSource2Version << '\n';

  const auto start = std::chrono::steady_clock::now();
  while (!stop_requested.load()) {
    if (options.run_for_ms.has_value()) {
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start);
      if (elapsed.count() >= *options.run_for_ms) {
        break;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  service.stop();
  eq2::core::log(log, eq2::core::LogLevel::info, "world", "source2 world server stopped");
  return EXIT_SUCCESS;
}

auto run_config_validation(const eq2::core::ConfigProvider& config) -> int {
  auto runtime = eq2::core::load_runtime_config(config);
  if (!runtime.has_value()) {
    std::cerr << runtime.error().message << '\n';
    return EXIT_FAILURE;
  }

  eq2::core::ConsoleLogSink log;
  eq2::core::log(log, eq2::core::LogLevel::info, "world",
                 "source2 world composition root validated");

  std::cout << "eq2_world_server source2 wiring "
            << "version=" << eq2::core::kSource2Version << ' '
            << runtime.value().world_listen.address << ':' << runtime.value().world_listen.port
            << " login=" << runtime.value().login_remote.address << ':'
            << runtime.value().login_remote.port << '\n';
  return EXIT_SUCCESS;
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
  auto options = parse_arguments(argc, argv);
  if (!options.has_value()) {
    std::cerr << options.error().message << '\n';
    return EXIT_FAILURE;
  }

  if (options.value().command == Command::help) {
    print_help();
    return EXIT_SUCCESS;
  }

  if (options.value().command == Command::smoke_world_live) {
    return run_live_smoke();
  }

  auto config = load_app_config(options.value());
  if (!config.has_value()) {
    std::cerr << config.error().message << '\n';
    return EXIT_FAILURE;
  }

  switch (options.value().command) {
    case Command::validate_config:
      return run_config_validation(config.value());
    case Command::serve:
      return run_server(config.value(), options.value());
    case Command::check_world_db:
      return run_world_db_check(config.value());
    case Command::smoke_world_live:
    case Command::help:
      break;
  }

  return EXIT_FAILURE;
}

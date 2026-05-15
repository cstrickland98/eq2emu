#include <eq2/core/config.h>
#include <eq2/core/log.h>
#include <eq2/core/runtime_config.h>
#include <eq2/core/version.h>
#include <eq2/db/query.h>
#include <eq2/db/sql_repositories.h>
#include <eq2/login/authentication.h>
#include <eq2/login/live_login.h>
#include <eq2/protocol/application_packet.h>
#include <eq2/protocol/login_request.h>
#include <eq2/protocol/login_response.h>
#include <eq2/protocol/opcode_version.h>
#include <eq2/protocol/packet_buffer.h>
#include <eq2/protocol/protocol_packet.h>
#include <eq2/protocol/session.h>

#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace {

enum class Command {
  validate_config,
  serve,
  check_login_db,
  smoke_login_live,
  smoke_login_mariadb,
  probe_login,
  help,
};

struct AppOptions {
  Command command = Command::validate_config;
  std::optional<std::string> config_path;
  std::vector<std::pair<std::string, std::string>> overrides;
  std::string username = "tester";
  std::string password = "correct";
  bool skip_account_auth = false;
  std::uint16_t client_version = 546;
  std::optional<std::uint32_t> expected_world_count;
  std::optional<std::uint32_t> run_for_ms;
};

std::atomic_bool stop_requested = false;

void request_stop(int) {
  stop_requested = true;
}

auto parse_u16(std::string_view value) -> std::optional<std::uint16_t> {
  auto parsed = 0U;
  auto digits = value;
  auto base = 10;
  if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) {
    digits.remove_prefix(2);
    base = 16;
  }
  if (digits.empty()) {
    return std::nullopt;
  }

  const auto* begin = digits.data();
  const auto* end = digits.data() + digits.size();
  const auto [ptr, error] = std::from_chars(begin, end, parsed, base);
  if (error != std::errc{} || ptr != end || parsed > 0xffffU) {
    return std::nullopt;
  }

  return static_cast<std::uint16_t>(parsed);
}

auto parse_u32(std::string_view value) -> std::optional<std::uint32_t> {
  auto parsed = std::uint32_t{0};
  const auto* begin = value.data();
  const auto* end = value.data() + value.size();
  const auto [ptr, error] = std::from_chars(begin, end, parsed);
  if (error != std::errc{} || ptr != end) {
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
  apply_env(config, "EQ2_LOGIN_ADDRESS", "login.address");
  apply_env(config, "EQ2_LOGIN_PORT", "login.port");
  apply_env(config, "EQ2_LOGIN_ACCOUNT_CREATION_ALLOWED", "login.account_creation_allowed");
  apply_env(config, "EQ2_LOGIN_CLIENT_VERSION", "login.client_version");
  apply_env(config, "EQ2_LOGIN_OPCODE_SOURCE", "login.opcode_source");
  apply_env(config, "EQ2_LOGIN_OPCODE_WIDTH", "login.opcode_width");
  apply_env(config, "EQ2_LOGIN_REQUEST_OPCODE", "login.request_opcode");
  apply_env(config, "EQ2_LOGIN_REPLY_OPCODE", "login.reply_opcode");
  apply_env(config, "EQ2_LOGIN_WORLD_LIST_OPCODE", "login.world_list_opcode");
  apply_env(config, "EQ2_LOGIN_ALL_WORLDS_REQUEST_OPCODE", "login.all_worlds_request_opcode");
  apply_env(config, "EQ2_LOGIN_CHARACTERS_REQUEST_OPCODE", "login.characters_request_opcode");
  apply_env(config, "EQ2_LOGIN_CHARACTERS_REPLY_OPCODE", "login.characters_reply_opcode");

  apply_env(config, "EQ2_DB_HOST", "db.host");
  apply_env(config, "EQ2_DB_PORT", "db.port");
  apply_env(config, "EQ2_DB_USER", "db.user");
  apply_env(config, "EQ2_DB_PASSWORD", "db.password");
  apply_env(config, "EQ2_DB_TLS", "db.tls");
  apply_env(config, "EQ2_LOGIN_DB_NAME", "db.name");
  if (!config.get("db.name").has_value()) {
    apply_env(config, "EQ2_DB_NAME", "db.name");
  }
}

void apply_overrides(eq2::core::MapConfig& config, const AppOptions& options) {
  for (const auto& [key, value] : options.overrides) {
    config.set(key, value);
  }
}

void print_help() {
  std::cout
      << "Usage: eq2_login_server [options]\n"
      << "\n"
      << "Commands:\n"
      << "  --serve                     Start the source2 login server until Ctrl+C.\n"
      << "  --check-login-db            Check MariaDB connectivity, opcodes, and login credentials.\n"
      << "  --probe-login               Connect to a running source2 login UDP listener and attempt login.\n"
      << "  --smoke-login-mariadb       Start an ephemeral source2 login server backed by MariaDB and login once.\n"
      << "  --smoke-login-live          Run the in-memory loopback smoke test used by CI.\n"
      << "  --validate-config           Validate config and exit (default).\n"
      << "  --help                      Show this help.\n"
      << "\n"
      << "Config:\n"
      << "  Precedence: INI config, environment variables, then CLI overrides.\n"
      << "  --config <path>             Load INI config. Sections become key prefixes.\n"
      << "  --set <key=value>           Override any config key.\n"
      << "  --login-address <address>   Login listen address, e.g. 0.0.0.0 or 127.0.0.1.\n"
      << "  --login-port <port>         Login listen port.\n"
      << "  --client-version <version>  Login client version/opcode range, default 546.\n"
      << "  --account-creation <bool>   Enable login account creation.\n"
      << "  --login-opcode-source <database|config> Source login app opcodes.\n"
      << "  --login-opcode-width <1|2>  Login application opcode width.\n"
      << "  --login-request-opcode <n>  OP_LoginRequestMsg EQ opcode, decimal or 0x hex.\n"
      << "  --login-reply-opcode <n>    OP_LoginReplyMsg EQ opcode, decimal or 0x hex.\n"
      << "  --login-world-list-opcode <n> OP_WorldListMsg EQ opcode, decimal or 0x hex.\n"
      << "  --login-all-worlds-request-opcode <n> OP_AllWSDescRequestMsg EQ opcode.\n"
      << "  --login-characters-request-opcode <n> OP_AllCharactersDescRequestMsg EQ opcode.\n"
      << "  --login-characters-reply-opcode <n> OP_AllCharactersDescReplyMsg EQ opcode.\n"
      << "  --db-host <host>            MariaDB host.\n"
      << "  --db-port <port>            MariaDB port.\n"
      << "  --db-name <name>            MariaDB login database name.\n"
      << "  --db-user <user>            MariaDB username.\n"
      << "  --db-password <password>    MariaDB password.\n"
      << "  --db-tls <bool>             Enable MariaDB TLS, default false.\n"
      << "\n"
      << "Probe/smoke login:\n"
      << "  --username <name>           Login username.\n"
      << "  --password <password>       Login password.\n"
      << "  --skip-account-auth         With --check-login-db, skip account authentication.\n"
      << "  --client-version <version>  Login client version/opcode range, default 546.\n"
      << "  --connect-host <host>       Host used by --probe-login.\n"
      << "  --connect-port <port>       Port used by --probe-login.\n"
      << "  --expect-world-count <n>    Require the world-list reply to contain n worlds.\n"
      << "  --run-for-ms <ms>           Stop --serve automatically after this many milliseconds.\n"
      << "\n"
      << "Environment aliases:\n"
      << "  EQ2_LOGIN_ADDRESS, EQ2_LOGIN_PORT, EQ2_LOGIN_ACCOUNT_CREATION_ALLOWED,\n"
      << "  EQ2_LOGIN_CLIENT_VERSION, EQ2_LOGIN_OPCODE_SOURCE, EQ2_LOGIN_OPCODE_WIDTH,\n"
      << "  EQ2_LOGIN_REQUEST_OPCODE,\n"
      << "  EQ2_LOGIN_REPLY_OPCODE, EQ2_LOGIN_WORLD_LIST_OPCODE,\n"
      << "  EQ2_LOGIN_ALL_WORLDS_REQUEST_OPCODE, EQ2_LOGIN_CHARACTERS_REQUEST_OPCODE,\n"
      << "  EQ2_LOGIN_CHARACTERS_REPLY_OPCODE,\n"
      << "  EQ2_LOGIN_USERNAME, EQ2_LOGIN_PASSWORD,\n"
      << "  EQ2_DB_HOST, EQ2_DB_PORT, EQ2_LOGIN_DB_NAME, EQ2_DB_NAME,\n"
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
  if (auto username = getenv_string("EQ2_LOGIN_USERNAME")) {
    options.username = std::move(*username);
  }
  if (auto password = getenv_string("EQ2_LOGIN_PASSWORD")) {
    options.password = std::move(*password);
  }

  for (auto index = 1; index < argc; ++index) {
    const auto arg = std::string_view(argv[index]);
    const auto require_value = [&](std::string_view option) -> std::optional<std::string> {
      if (index + 1 >= argc) {
        return std::nullopt;
      }
      ++index;
      (void)option;
      return std::string(argv[index]);
    };

    if (arg == "--help" || arg == "-h") {
      options.command = Command::help;
    } else if (arg == "--serve") {
      options.command = Command::serve;
    } else if (arg == "--check-login-db") {
      options.command = Command::check_login_db;
    } else if (arg == "--skip-account-auth") {
      options.skip_account_auth = true;
    } else if (arg == "--validate-config") {
      options.command = Command::validate_config;
    } else if (arg == "--smoke-login-live") {
      options.command = Command::smoke_login_live;
    } else if (arg == "--smoke-login-mariadb") {
      options.command = Command::smoke_login_mariadb;
    } else if (arg == "--probe-login") {
      options.command = Command::probe_login;
    } else if (arg == "--strict-config") {
      add_key_value_override(options, "login.port", "invalid");
    } else if (arg == "--config") {
      auto value = require_value(arg);
      if (!value.has_value()) {
        return eq2::core::Result<AppOptions>::failure(eq2::core::Error{
            .code = eq2::core::ErrorCode::invalid_argument,
            .message = "--config requires a path",
        });
      }
      options.config_path = std::move(*value);
    } else if (arg == "--set") {
      auto value = require_value(arg);
      if (!value.has_value()) {
        return eq2::core::Result<AppOptions>::failure(eq2::core::Error{
            .code = eq2::core::ErrorCode::invalid_argument,
            .message = "--set requires key=value",
        });
      }
      const auto separator = value->find('=');
      if (separator == std::string::npos) {
        return eq2::core::Result<AppOptions>::failure(eq2::core::Error{
            .code = eq2::core::ErrorCode::invalid_argument,
            .message = "--set requires key=value",
        });
      }
      add_key_value_override(options, value->substr(0, separator), value->substr(separator + 1));
    } else if (arg == "--login-address") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--login-address requires a value"});
      }
      add_key_value_override(options, "login.address", std::move(*value));
    } else if (arg == "--login-port") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--login-port requires a value"});
      }
      add_key_value_override(options, "login.port", std::move(*value));
    } else if (arg == "--account-creation") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--account-creation requires a value"});
      }
      add_key_value_override(options, "login.account_creation_allowed", std::move(*value));
    } else if (arg == "--login-opcode-source") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--login-opcode-source requires a value"});
      }
      add_key_value_override(options, "login.opcode_source", std::move(*value));
    } else if (arg == "--login-opcode-width") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--login-opcode-width requires a value"});
      }
      add_key_value_override(options, "login.opcode_width", std::move(*value));
    } else if (arg == "--login-request-opcode") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--login-request-opcode requires a value"});
      }
      add_key_value_override(options, "login.request_opcode", std::move(*value));
    } else if (arg == "--login-reply-opcode") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--login-reply-opcode requires a value"});
      }
      add_key_value_override(options, "login.reply_opcode", std::move(*value));
    } else if (arg == "--login-world-list-opcode") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--login-world-list-opcode requires a value"});
      }
      add_key_value_override(options, "login.world_list_opcode", std::move(*value));
    } else if (arg == "--login-all-worlds-request-opcode") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--login-all-worlds-request-opcode requires a value"});
      }
      add_key_value_override(options, "login.all_worlds_request_opcode", std::move(*value));
    } else if (arg == "--login-characters-request-opcode") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--login-characters-request-opcode requires a value"});
      }
      add_key_value_override(options, "login.characters_request_opcode", std::move(*value));
    } else if (arg == "--login-characters-reply-opcode") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--login-characters-reply-opcode requires a value"});
      }
      add_key_value_override(options, "login.characters_reply_opcode", std::move(*value));
    } else if (arg == "--db-host") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--db-host requires a value"});
      }
      add_key_value_override(options, "db.host", std::move(*value));
    } else if (arg == "--db-port") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--db-port requires a value"});
      }
      add_key_value_override(options, "db.port", std::move(*value));
    } else if (arg == "--db-name") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--db-name requires a value"});
      }
      add_key_value_override(options, "db.name", std::move(*value));
    } else if (arg == "--db-user") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--db-user requires a value"});
      }
      add_key_value_override(options, "db.user", std::move(*value));
    } else if (arg == "--db-password") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--db-password requires a value"});
      }
      add_key_value_override(options, "db.password", std::move(*value));
    } else if (arg == "--db-tls") {
      auto value = require_value(arg);
      if (!value || !parse_bool(*value).has_value()) {
        return eq2::core::Result<AppOptions>::failure({.message = "--db-tls requires a boolean"});
      }
      add_key_value_override(options, "db.tls", std::move(*value));
    } else if (arg == "--connect-host") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--connect-host requires a value"});
      }
      add_key_value_override(options, "connect.host", std::move(*value));
    } else if (arg == "--connect-port") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--connect-port requires a value"});
      }
      add_key_value_override(options, "connect.port", std::move(*value));
    } else if (arg == "--username") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--username requires a value"});
      }
      options.username = std::move(*value);
    } else if (arg == "--password") {
      auto value = require_value(arg);
      if (!value) {
        return eq2::core::Result<AppOptions>::failure({.message = "--password requires a value"});
      }
      options.password = std::move(*value);
    } else if (arg == "--client-version") {
      auto value = require_value(arg);
      const auto parsed = value.has_value() ? parse_u16(*value) : std::nullopt;
      if (!parsed.has_value()) {
        return eq2::core::Result<AppOptions>::failure({.message = "--client-version requires a port-sized integer"});
      }
      options.client_version = *parsed;
      add_key_value_override(options, "login.client_version", std::move(*value));
    } else if (arg == "--expect-world-count") {
      auto value = require_value(arg);
      const auto parsed = value.has_value() ? parse_u32(*value) : std::nullopt;
      if (!parsed.has_value()) {
        return eq2::core::Result<AppOptions>::failure({.message = "--expect-world-count requires an integer"});
      }
      options.expected_world_count = *parsed;
    } else if (arg == "--run-for-ms") {
      auto value = require_value(arg);
      const auto parsed = value.has_value() ? parse_u32(*value) : std::nullopt;
      if (!parsed.has_value()) {
        return eq2::core::Result<AppOptions>::failure({.message = "--run-for-ms requires an integer"});
      }
      options.run_for_ms = *parsed;
    } else {
      return eq2::core::Result<AppOptions>::failure(eq2::core::Error{
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

auto apply_configured_app_options(AppOptions& options,
                                  const eq2::core::ConfigProvider& config)
    -> eq2::core::Result<void> {
  if (auto value = config.get("login.client_version")) {
    auto parsed = parse_u16(*value);
    if (!parsed.has_value()) {
      return eq2::core::Result<void>::failure(eq2::core::Error{
          .code = eq2::core::ErrorCode::invalid_argument,
          .message = "login.client_version must be an integer from 0 to 65535",
      });
    }
    options.client_version = *parsed;
  }

  return eq2::core::Result<void>::success();
}

auto supported_login_versions() -> eq2::protocol::OpcodeVersionRanges {
  eq2::protocol::OpcodeVersionRanges ranges;
  ranges.add_range(546, 561);
  return ranges;
}

auto supported_login_versions_for_config(std::uint16_t client_version)
    -> eq2::protocol::OpcodeVersionRanges {
  auto ranges = supported_login_versions();
  if (!ranges.contains_client_version(static_cast<std::int16_t>(client_version))) {
    ranges.add_range(static_cast<std::int16_t>(client_version),
                     static_cast<std::int16_t>(client_version));
  }
  return ranges;
}

auto load_supported_login_versions_from_database(eq2::db::QueryConnection& connection,
                                                 std::uint16_t client_version)
    -> eq2::core::Result<eq2::protocol::OpcodeVersionRanges> {
  auto loaded = eq2::db::load_login_opcode_version_ranges(
      connection, static_cast<std::int16_t>(client_version));
  if (!loaded.has_value()) {
    return eq2::core::Result<eq2::protocol::OpcodeVersionRanges>::failure(loaded.error());
  }

  eq2::protocol::OpcodeVersionRanges ranges;
  for (const auto& range : loaded.value()) {
    ranges.add_range(range.min_version, range.max_version);
  }
  return eq2::core::Result<eq2::protocol::OpcodeVersionRanges>::success(ranges);
}

auto load_supported_login_versions(const eq2::core::ConfigProvider& config,
                                   eq2::db::QueryConnection& connection,
                                   std::uint16_t client_version)
    -> eq2::core::Result<eq2::protocol::OpcodeVersionRanges> {
  const auto source = config.get("login.opcode_source").value_or("database");
  if (source == "config") {
    return eq2::core::Result<eq2::protocol::OpcodeVersionRanges>::success(
        supported_login_versions_for_config(client_version));
  }

  if (source == "database") {
    return load_supported_login_versions_from_database(connection, client_version);
  }

  return eq2::core::Result<eq2::protocol::OpcodeVersionRanges>::failure(eq2::core::Error{
      .code = eq2::core::ErrorCode::invalid_argument,
      .message = "login.opcode_source must be database or config",
  });
}

auto load_database_config(const eq2::core::ConfigProvider& config) -> eq2::db::DatabaseConfig {
  return eq2::db::DatabaseConfig{
      .host = config.get("db.host").value_or("127.0.0.1"),
      .port = parse_u16(config.get("db.port").value_or("3306")).value_or(3306),
      .database = config.get("db.name").value_or(""),
      .username = config.get("db.user").value_or(""),
      .password = config.get("db.password").value_or(""),
      .use_tls = parse_bool(config.get("db.tls").value_or("false")).value_or(false),
  };
}

auto parse_opcode_width(std::string_view value)
    -> std::optional<eq2::protocol::ApplicationOpcodeWidth> {
  if (value == "1" || value == "one" || value == "one_byte" || value == "one-byte" ||
      value == "login_stream") {
    return eq2::protocol::ApplicationOpcodeWidth::one_byte;
  }
  if (value == "2" || value == "two" || value == "two_byte" || value == "two-byte" ||
      value == "two_bytes" || value == "world_stream") {
    return eq2::protocol::ApplicationOpcodeWidth::two_bytes;
  }

  return std::nullopt;
}

auto config_first(const eq2::core::ConfigProvider& config,
                  std::initializer_list<std::string_view> keys) -> std::optional<std::string> {
  for (const auto key : keys) {
    if (auto value = config.get(key)) {
      return value;
    }
  }

  return std::nullopt;
}

auto validate_live_login_options(const eq2::login::LiveLoginOptions& options)
    -> std::optional<std::string> {
  if (options.opcode_width == eq2::protocol::ApplicationOpcodeWidth::one_byte &&
      (options.login_request_opcode > 0xffU || options.login_reply_opcode > 0xffU ||
       options.world_list_reply_opcode > 0xffU || options.all_worlds_request_opcode > 0xffU ||
       options.characters_request_opcode > 0xffU || options.characters_reply_opcode > 0xffU)) {
    return "one-byte login opcode width requires all configured login opcodes to be <= 255";
  }

  return std::nullopt;
}

auto load_live_login_options(const eq2::core::ConfigProvider& config,
                             bool validate_opcode_range = true)
    -> eq2::core::Result<eq2::login::LiveLoginOptions> {
  auto options = eq2::login::LiveLoginOptions{};

  if (auto value = config_first(config, {"login.opcode_width", "login.application_opcode_width"})) {
    auto parsed = parse_opcode_width(*value);
    if (!parsed.has_value()) {
      return eq2::core::Result<eq2::login::LiveLoginOptions>::failure(eq2::core::Error{
          .code = eq2::core::ErrorCode::invalid_argument,
          .message = "login opcode width must be 1, 2, one_byte, or two_bytes",
      });
    }
    options.opcode_width = *parsed;
  }

  const auto read_opcode =
      [&config](std::initializer_list<std::string_view> keys,
                std::string_view label,
                std::uint16_t fallback)
      -> eq2::core::Result<std::uint16_t> {
    auto value = config_first(config, keys);
    if (!value.has_value()) {
      return eq2::core::Result<std::uint16_t>::success(fallback);
    }

    auto parsed = parse_u16(*value);
    if (!parsed.has_value()) {
      return eq2::core::Result<std::uint16_t>::failure(eq2::core::Error{
          .code = eq2::core::ErrorCode::invalid_argument,
          .message = std::string(label) + " must be an integer from 0 to 65535",
      });
    }
    return eq2::core::Result<std::uint16_t>::success(*parsed);
  };

  auto request = read_opcode({"login.request_opcode", "login.login_request_opcode"},
                             "login request opcode",
                             options.login_request_opcode);
  auto reply = read_opcode({"login.reply_opcode", "login.login_reply_opcode"},
                           "login reply opcode",
                           options.login_reply_opcode);
  auto world_list = read_opcode({"login.world_list_opcode", "login.worldlist_opcode"},
                                "login world-list opcode",
                                options.world_list_reply_opcode);
  auto all_worlds_request = read_opcode(
      {"login.all_worlds_request_opcode", "login.allws_request_opcode"},
      "login all-worlds request opcode",
      options.all_worlds_request_opcode);
  auto characters_reply = read_opcode(
      {"login.characters_reply_opcode", "login.character_list_opcode"},
      "login characters reply opcode",
      options.characters_reply_opcode);
  auto characters_request = read_opcode(
      {"login.characters_request_opcode", "login.character_list_request_opcode"},
      "login characters request opcode",
      options.characters_request_opcode);
  if (!request.has_value()) {
    return eq2::core::Result<eq2::login::LiveLoginOptions>::failure(request.error());
  }
  if (!reply.has_value()) {
    return eq2::core::Result<eq2::login::LiveLoginOptions>::failure(reply.error());
  }
  if (!world_list.has_value()) {
    return eq2::core::Result<eq2::login::LiveLoginOptions>::failure(world_list.error());
  }
  if (!all_worlds_request.has_value()) {
    return eq2::core::Result<eq2::login::LiveLoginOptions>::failure(all_worlds_request.error());
  }
  if (!characters_reply.has_value()) {
    return eq2::core::Result<eq2::login::LiveLoginOptions>::failure(characters_reply.error());
  }
  if (!characters_request.has_value()) {
    return eq2::core::Result<eq2::login::LiveLoginOptions>::failure(characters_request.error());
  }

  options.login_request_opcode = request.value();
  options.login_reply_opcode = reply.value();
  options.world_list_reply_opcode = world_list.value();
  options.all_worlds_request_opcode = all_worlds_request.value();
  options.characters_request_opcode = characters_request.value();
  options.characters_reply_opcode = characters_reply.value();

  if (validate_opcode_range) {
    if (auto error = validate_live_login_options(options)) {
      return eq2::core::Result<eq2::login::LiveLoginOptions>::failure(eq2::core::Error{
          .code = eq2::core::ErrorCode::invalid_argument,
          .message = *error,
      });
    }
  }

  return eq2::core::Result<eq2::login::LiveLoginOptions>::success(options);
}

auto validate_login_opcode_source(const eq2::core::ConfigProvider& config)
    -> eq2::core::Result<void> {
  const auto source = config.get("login.opcode_source").value_or("database");
  if (source == "database" || source == "config") {
    return eq2::core::Result<void>::success();
  }

  return eq2::core::Result<void>::failure(eq2::core::Error{
      .code = eq2::core::ErrorCode::invalid_argument,
      .message = "login.opcode_source must be database or config",
  });
}

auto load_live_login_options_from_database(const eq2::core::ConfigProvider& config,
                                           eq2::db::QueryConnection& connection,
                                           std::uint16_t client_version)
    -> eq2::core::Result<eq2::login::LiveLoginOptions> {
  auto source = validate_login_opcode_source(config);
  if (!source.has_value()) {
    return eq2::core::Result<eq2::login::LiveLoginOptions>::failure(eq2::core::Error{
        .code = source.error().code,
        .message = source.error().message,
    });
  }

  const auto source_value = config.get("login.opcode_source").value_or("database");
  auto options = load_live_login_options(config, source_value == "config");
  if (!options.has_value()) {
    return options;
  }

  if (source_value == "config") {
    return options;
  }

  auto opcodes = eq2::db::load_login_opcode_set(
      connection, static_cast<std::int16_t>(client_version));
  if (!opcodes.has_value()) {
    return eq2::core::Result<eq2::login::LiveLoginOptions>::failure(opcodes.error());
  }

  auto live_options = options.value();
  live_options.login_request_opcode = opcodes.value().login_request_opcode;
  live_options.login_reply_opcode = opcodes.value().login_reply_opcode;
  live_options.world_list_reply_opcode = opcodes.value().world_list_opcode;
  live_options.all_worlds_request_opcode = opcodes.value().all_worlds_request_opcode;
  live_options.characters_request_opcode = opcodes.value().characters_request_opcode;
  live_options.characters_reply_opcode = opcodes.value().characters_reply_opcode;
  if (auto error = validate_live_login_options(live_options)) {
    return eq2::core::Result<eq2::login::LiveLoginOptions>::failure(eq2::core::Error{
        .code = eq2::core::ErrorCode::invalid_argument,
        .message = *error,
    });
  }

  return eq2::core::Result<eq2::login::LiveLoginOptions>::success(live_options);
}

auto validate_database_config(const eq2::core::ConfigProvider& provider,
                              const eq2::db::DatabaseConfig& config)
    -> std::optional<std::string> {
  if (auto port = provider.get("db.port"); port.has_value() && !parse_u16(*port).has_value()) {
    return "MariaDB port must be an integer from 0 to 65535";
  }
  if (auto tls = provider.get("db.tls"); tls.has_value() && !parse_bool(*tls).has_value()) {
    return "MariaDB TLS setting must be a boolean";
  }
  if (config.database.empty()) {
    return "MariaDB login database is required; use --db-name, db.name, EQ2_LOGIN_DB_NAME, or EQ2_DB_NAME";
  }
  if (config.username.empty()) {
    return "MariaDB user is required; use --db-user, db.user, or EQ2_DB_USER";
  }
  return std::nullopt;
}

auto preflight_database(const std::shared_ptr<eq2::db::QueryConnection>& connection)
    -> eq2::core::Result<void> {
  auto result = connection->execute(eq2::db::QueryRequest{.sql = "select 1"});
  if (!result.has_value()) {
    return eq2::core::Result<void>::failure(result.error());
  }
  return eq2::core::Result<void>::success();
}

auto make_login_frame(std::string_view username,
                      std::string_view password,
                      std::uint16_t client_version,
                      const eq2::login::LiveLoginOptions& live_options) -> std::vector<std::uint8_t> {
  const auto login_payload = eq2::protocol::encode_legacy_login_request_fixture(
      eq2::protocol::LoginRequest{
          .access_code = "station",
          .username = std::string(username),
          .password = std::string(password),
          .version = static_cast<std::int16_t>(client_version),
      });
  const auto app_packet = eq2::protocol::encode_application_packet(
      live_options.login_request_opcode, login_payload, live_options.opcode_width);
  eq2::protocol::PacketWriter writer;
  writer.append_u16_be(0);
  writer.append_bytes(app_packet);
  return eq2::protocol::encode_protocol_packet(eq2::protocol::kOpPacket, writer.bytes());
}

auto make_all_worlds_request_frame(const eq2::login::LiveLoginOptions& live_options)
    -> std::vector<std::uint8_t> {
  const auto app_packet = eq2::protocol::encode_application_packet(
      live_options.all_worlds_request_opcode,
      std::span<const std::uint8_t>{},
      live_options.opcode_width);
  eq2::protocol::PacketWriter writer;
  writer.append_u16_be(1);
  writer.append_bytes(app_packet);
  return eq2::protocol::encode_protocol_packet(eq2::protocol::kOpPacket, writer.bytes());
}

auto decode_login_reply(std::span<const std::uint8_t> response,
                        const eq2::login::LiveLoginOptions& live_options)
    -> std::optional<eq2::protocol::LoginReplyPayload> {
  return eq2::protocol::decode_login_reply_protocol_frame(
      response, live_options.login_reply_opcode, live_options.opcode_width);
}

auto decode_world_list_response(std::span<const std::uint8_t> response,
                                const eq2::login::LiveLoginOptions& live_options)
    -> std::optional<eq2::protocol::LoginWorldListPayload> {
  const auto protocol = eq2::protocol::decode_protocol_packet(response);
  if (!protocol.has_value() || protocol->opcode != eq2::protocol::kOpPacket) {
    return std::nullopt;
  }

  const auto decode_app =
      [&live_options](std::span<const std::uint8_t> payload)
      -> std::optional<eq2::protocol::LoginWorldListPayload> {
    const auto app = eq2::protocol::decode_application_packet(payload, live_options.opcode_width);
    if (!app.has_value() || app->opcode != live_options.world_list_reply_opcode) {
      return std::nullopt;
    }

    return eq2::protocol::decode_login_world_list_payload(app->payload);
  };

  if (protocol->payload.size() >= 2) {
    if (auto sequenced = decode_app(protocol->payload.subspan(2))) {
      return sequenced;
    }
  }

  return decode_app(protocol->payload);
}

auto decode_character_list_account_id(std::span<const std::uint8_t> response,
                                      const eq2::login::LiveLoginOptions& live_options)
    -> std::optional<std::uint32_t> {
  const auto protocol = eq2::protocol::decode_protocol_packet(response);
  if (!protocol.has_value() || protocol->opcode != eq2::protocol::kOpPacket) {
    return std::nullopt;
  }

  const auto decode_app =
      [&live_options](std::span<const std::uint8_t> payload) -> std::optional<std::uint32_t> {
    const auto app = eq2::protocol::decode_application_packet(payload, live_options.opcode_width);
    if (!app.has_value() || app->opcode != live_options.characters_reply_opcode ||
        app->payload.size() < 5) {
      return std::nullopt;
    }

    eq2::protocol::PacketReader reader(app->payload);
    const auto character_count = reader.read_u8();
    const auto account_id = reader.read_u32_le();
    if (!character_count.has_value() || !account_id.has_value() || *character_count != 0) {
      return std::nullopt;
    }
    return account_id;
  };

  if (protocol->payload.size() >= 2) {
    if (auto sequenced = decode_app(protocol->payload.subspan(2))) {
      return sequenced;
    }
  }

  return decode_app(protocol->payload);
}

struct LoginProbeExchange {
  eq2::protocol::LoginReplyPayload reply;
  bool world_list_received = false;
  bool character_list_received = false;
  bool post_world_reply_received = false;
  std::size_t world_count = 0;
  std::uint32_t character_list_account_id = 0;
};

auto exchange_login_sequence(eq2::net::SocketEndpoint endpoint,
                             std::string_view username,
                             std::string_view password,
                             std::uint16_t client_version,
                             const eq2::login::LiveLoginOptions& live_options)
    -> std::optional<LoginProbeExchange> {
  eq2::net::UdpSocketClient client;
  if (!client.connect_to(std::move(endpoint))) {
    return std::nullopt;
  }

  const auto session_request = eq2::protocol::encode_protocol_packet(
      eq2::protocol::kOpSessionRequest,
      eq2::protocol::encode_session_request(eq2::protocol::SessionRequest{
          .unknown_a = 0,
          .session = 0x10203040,
          .max_length = 512,
      }));
  if (!client.send(session_request)) {
    return std::nullopt;
  }

  const auto session_response = client.receive();
  if (!session_response.has_value()) {
    return std::nullopt;
  }

  const auto protocol = eq2::protocol::decode_protocol_packet(*session_response);
  if (!protocol.has_value() || protocol->opcode != eq2::protocol::kOpSessionResponse) {
    return std::nullopt;
  }

  if (!client.send(make_login_frame(username, password, client_version, live_options))) {
    return std::nullopt;
  }

  auto exchange = std::optional<LoginProbeExchange>{};
  for (auto attempt = 0; attempt < 4; ++attempt) {
    const auto login_response = client.receive();
    if (!login_response.has_value()) {
      return std::nullopt;
    }

    if (auto reply = decode_login_reply(*login_response, live_options)) {
      exchange = LoginProbeExchange{.reply = *reply};
      break;
    }
  }

  if (!exchange.has_value()) {
    return std::nullopt;
  }

  if (exchange->reply.reply_code !=
      static_cast<std::uint8_t>(eq2::login::LoginReplyCode::accepted)) {
    return exchange;
  }

  if (!client.send(make_all_worlds_request_frame(live_options))) {
    return std::nullopt;
  }

  for (auto attempt = 0; attempt < 8 &&
                         (!exchange->world_list_received ||
                          !exchange->character_list_received ||
                          !exchange->post_world_reply_received);
       ++attempt) {
    const auto response = client.receive();
    if (!response.has_value()) {
      break;
    }

    const auto response_protocol = eq2::protocol::decode_protocol_packet(*response);
    if (response_protocol.has_value() && response_protocol->opcode == eq2::protocol::kOpAck) {
      continue;
    }

    if (!exchange->world_list_received) {
      if (auto world_list = decode_world_list_response(*response, live_options)) {
        exchange->world_list_received = true;
        exchange->world_count = world_list->worlds.size();
        continue;
      }
    }

    if (!exchange->character_list_received) {
      if (auto account_id = decode_character_list_account_id(*response, live_options)) {
        exchange->character_list_received = true;
        exchange->character_list_account_id = *account_id;
        continue;
      }
    }

    if (!exchange->post_world_reply_received) {
      if (auto reply = decode_login_reply(*response, live_options);
          reply.has_value() && reply->reply_code == 10) {
        exchange->post_world_reply_received = true;
      }
    }
  }

  return exchange;
}

auto exchange_login(eq2::net::SocketEndpoint endpoint,
                    std::string_view username,
                    std::string_view password,
                    std::uint16_t client_version,
                    const eq2::login::LiveLoginOptions& live_options)
    -> std::optional<eq2::protocol::LoginReplyPayload> {
  auto exchange = exchange_login_sequence(
      std::move(endpoint), username, password, client_version, live_options);
  if (!exchange.has_value()) {
    return std::nullopt;
  }
  return exchange->reply;
}

auto load_probe_login_options(const eq2::core::ConfigProvider& config,
                              std::uint16_t client_version)
    -> eq2::core::Result<eq2::login::LiveLoginOptions> {
  const auto source = config.get("login.opcode_source").value_or("config");
  if (source == "config") {
    return load_live_login_options(config);
  }
  if (source != "database") {
    return eq2::core::Result<eq2::login::LiveLoginOptions>::failure(eq2::core::Error{
        .code = eq2::core::ErrorCode::invalid_argument,
        .message = "login.opcode_source must be database or config",
    });
  }

  auto db_config = load_database_config(config);
  if (auto error = validate_database_config(config, db_config)) {
    return eq2::core::Result<eq2::login::LiveLoginOptions>::failure(eq2::core::Error{
        .code = eq2::core::ErrorCode::invalid_argument,
        .message = "probe with login.opcode_source=database requires DB config: " + *error,
    });
  }

  auto connection = std::make_shared<eq2::db::MariaDbConnection>(std::move(db_config));
  auto preflight = preflight_database(connection);
  if (!preflight.has_value()) {
    return eq2::core::Result<eq2::login::LiveLoginOptions>::failure(preflight.error());
  }
  return load_live_login_options_from_database(config, *connection, client_version);
}

auto wait_for_login_count(const eq2::login::LiveLoginService& service, std::size_t count) -> bool {
  for (auto attempt = 0; attempt < 80; ++attempt) {
    if (service.login_outcomes().size() >= count) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  return false;
}

auto validate_expected_world_count(const LoginProbeExchange& exchange,
                                   const AppOptions& options) -> bool {
  if (!options.expected_world_count.has_value()) {
    return true;
  }

  return exchange.world_count == *options.expected_world_count;
}

void print_expected_world_count_error(const LoginProbeExchange& exchange,
                                      const AppOptions& options) {
  if (!options.expected_world_count.has_value()) {
    return;
  }

  std::cerr << "world-list count mismatch: expected "
            << *options.expected_world_count << " got " << exchange.world_count << '\n';
}

auto run_live_smoke(const AppOptions& options) -> int {
  class SmokeLoginConnection final : public eq2::db::QueryConnection {
   public:
    SmokeLoginConnection(std::string username, std::string password)
        : username_(std::move(username)), password_(std::move(password)) {}

    auto execute(const eq2::db::QueryRequest& request)
        -> eq2::core::Result<eq2::db::QueryResult> override {
      if (request.sql.find("select id, name from account") != std::string::npos &&
          request.parameters.size() >= 2 && request.parameters[0] == username_ &&
          request.parameters[1] == password_) {
        return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{
            .rows =
                {
                    eq2::db::QueryRow{.columns = {{"id", "42"}, {"name", username_}}},
                },
            .affected_rows = 1,
        });
      }

      return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{});
    }

   private:
    std::string username_;
    std::string password_;
  };

  auto connection = std::make_shared<SmokeLoginConnection>(options.username, options.password);
  eq2::db::SqlLoginAccountRepository accounts(connection);
  const auto live_options = eq2::login::LiveLoginOptions{};
  eq2::login::LiveLoginService service(
      eq2::login::LoginServerConfig{.address = "127.0.0.1", .port = 0},
      accounts,
      supported_login_versions(),
      live_options);

  if (!service.start()) {
    std::cerr << "failed to start live login smoke transport\n";
    return EXIT_FAILURE;
  }

  const auto exchange = exchange_login_sequence(eq2::net::SocketEndpoint{
                                                   .address = "127.0.0.1",
                                                   .port = service.port(),
                                               },
                                               options.username,
                                               options.password,
                                               options.client_version,
                                               live_options);
  if (!exchange.has_value() || !wait_for_login_count(service, 1)) {
    service.stop();
    std::cerr << "live login smoke did not complete\n";
    return EXIT_FAILURE;
  }

  const auto outcomes = service.login_outcomes();
  service.stop();

  if (outcomes.empty() || outcomes.front().reply_code != eq2::login::LoginReplyCode::accepted ||
      !outcomes.front().account.has_value()) {
    std::cerr << "live login smoke returned unexpected outcome\n";
    return EXIT_FAILURE;
  }
  if (!exchange->world_list_received || !exchange->character_list_received ||
      !exchange->post_world_reply_received) {
    std::cerr << "live login smoke did not complete the post-login world description flow\n";
    return EXIT_FAILURE;
  }
  if (!validate_expected_world_count(*exchange, options)) {
    print_expected_world_count_error(*exchange, options);
    return EXIT_FAILURE;
  }

  std::cout << "smoke-login-live accepted account=" << outcomes.front().account->id
            << " worlds=" << exchange->world_count << '\n';
  return EXIT_SUCCESS;
}

auto run_probe_login(const eq2::core::ConfigProvider& config, const AppOptions& options) -> int {
  const auto runtime = eq2::core::load_runtime_config(config);
  if (!runtime.has_value()) {
    std::cerr << runtime.error().message << '\n';
    return EXIT_FAILURE;
  }

  const auto live_options = load_probe_login_options(config, options.client_version);
  if (!live_options.has_value()) {
    std::cerr << live_options.error().message << '\n';
    return EXIT_FAILURE;
  }

  auto host = config.get("connect.host").value_or(runtime.value().login_listen.address);
  if (host.empty() || host == "0.0.0.0") {
    host = "127.0.0.1";
  }
  const auto port = parse_u16(config.get("connect.port").value_or(""))
                        .value_or(runtime.value().login_listen.port);

  const auto exchange = exchange_login_sequence(eq2::net::SocketEndpoint{.address = host, .port = port},
                                                options.username,
                                                options.password,
                                                options.client_version,
                                                live_options.value());
  if (!exchange.has_value()) {
    std::cerr << "login probe failed to complete source2 session login against "
              << host << ':' << port << '\n';
    return EXIT_FAILURE;
  }

  std::cout << "login probe reply_code=" << static_cast<int>(exchange->reply.reply_code)
            << " account_id=" << exchange->reply.account_id;
  if (exchange->reply.reply_code ==
      static_cast<std::uint8_t>(eq2::login::LoginReplyCode::accepted)) {
    std::cout << " world_list=" << (exchange->world_list_received ? "yes" : "no")
              << " worlds=" << exchange->world_count
              << " character_list=" << (exchange->character_list_received ? "yes" : "no")
              << " post_world_reply=" << (exchange->post_world_reply_received ? "yes" : "no");
  }
  std::cout << '\n';

  if (exchange->reply.reply_code !=
      static_cast<std::uint8_t>(eq2::login::LoginReplyCode::accepted)) {
    return EXIT_FAILURE;
  }
  if (!validate_expected_world_count(*exchange, options)) {
    print_expected_world_count_error(*exchange, options);
    return EXIT_FAILURE;
  }
  return exchange->world_list_received && exchange->character_list_received &&
                 exchange->post_world_reply_received
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}

auto run_login_db_check(const eq2::core::ConfigProvider& config, const AppOptions& options)
    -> int {
  auto db_config = load_database_config(config);
  if (auto error = validate_database_config(config, db_config)) {
    std::cerr << *error << '\n';
    return EXIT_FAILURE;
  }

  auto connection = std::make_shared<eq2::db::MariaDbConnection>(std::move(db_config));
  auto preflight = preflight_database(connection);
  if (!preflight.has_value()) {
    std::cerr << preflight.error().message << '\n';
    return EXIT_FAILURE;
  }
  auto schema = eq2::db::preflight_login_database_schema(*connection);
  if (!schema.has_value()) {
    std::cerr << schema.error().message << '\n';
    return EXIT_FAILURE;
  }
  auto opcodes = eq2::db::load_login_opcode_set(
      *connection, static_cast<std::int16_t>(options.client_version));
  if (!opcodes.has_value()) {
    std::cerr << opcodes.error().message << '\n';
    return EXIT_FAILURE;
  }
  auto supported_versions = load_supported_login_versions_from_database(
      *connection, options.client_version);
  if (!supported_versions.has_value()) {
    std::cerr << supported_versions.error().message << '\n';
    return EXIT_FAILURE;
  }

  if (options.skip_account_auth) {
    std::cout << "login-db-check account_auth=skipped"
              << " request_opcode=" << opcodes.value().login_request_opcode
              << " reply_opcode=" << opcodes.value().login_reply_opcode
              << " world_list_opcode=" << opcodes.value().world_list_opcode
              << " all_worlds_request_opcode=" << opcodes.value().all_worlds_request_opcode
              << " characters_request_opcode=" << opcodes.value().characters_request_opcode
              << " characters_reply_opcode=" << opcodes.value().characters_reply_opcode;
    std::cout << '\n';
    return EXIT_SUCCESS;
  }

  eq2::db::SqlLoginAccountRepository accounts(connection);
  const auto request = eq2::login::LoginAuthenticationRequest{
      .credentials = eq2::login::LoginCredentials{
          .username = options.username,
          .password = options.password,
      },
      .client_version_is_supported = supported_versions.value().contains_client_version(
          static_cast<std::int16_t>(options.client_version)),
      .account_creation_is_allowed = false,
      .account_already_has_session = false,
  };
  const auto authenticated = eq2::login::authenticate_login(request, accounts);

  std::cout << "login-db-check reply_code=" << static_cast<int>(authenticated.reply_code);
  if (authenticated.account.has_value()) {
    std::cout << " account_id=" << authenticated.account->id;
  }
  std::cout << " request_opcode=" << opcodes.value().login_request_opcode
            << " reply_opcode=" << opcodes.value().login_reply_opcode
            << " world_list_opcode=" << opcodes.value().world_list_opcode
            << " all_worlds_request_opcode=" << opcodes.value().all_worlds_request_opcode
            << " characters_request_opcode=" << opcodes.value().characters_request_opcode
            << " characters_reply_opcode=" << opcodes.value().characters_reply_opcode;
  std::cout << '\n';

  return authenticated.reply_code == eq2::login::LoginReplyCode::accepted ? EXIT_SUCCESS
                                                                          : EXIT_FAILURE;
}

auto run_mariadb_smoke(const eq2::core::ConfigProvider& config, const AppOptions& options)
    -> int {
  auto db_config = load_database_config(config);
  if (auto error = validate_database_config(config, db_config)) {
    std::cerr << *error << '\n';
    return EXIT_FAILURE;
  }

  auto connection = std::make_shared<eq2::db::MariaDbConnection>(std::move(db_config));
  auto preflight = preflight_database(connection);
  if (!preflight.has_value()) {
    std::cerr << preflight.error().message << '\n';
    return EXIT_FAILURE;
  }
  auto schema = eq2::db::preflight_login_database_schema(*connection);
  if (!schema.has_value()) {
    std::cerr << schema.error().message << '\n';
    return EXIT_FAILURE;
  }

  eq2::db::SqlLoginAccountRepository accounts(connection);
  eq2::db::SqlWorldRegistrationRepository worlds(connection);
  auto live_options = load_live_login_options_from_database(
      config, *connection, options.client_version);
  if (!live_options.has_value()) {
    std::cerr << live_options.error().message << '\n';
    return EXIT_FAILURE;
  }
  auto supported_versions = load_supported_login_versions(
      config, *connection, options.client_version);
  if (!supported_versions.has_value()) {
    std::cerr << supported_versions.error().message << '\n';
    return EXIT_FAILURE;
  }
  eq2::login::LiveLoginService service(
      eq2::login::LoginServerConfig{.address = "127.0.0.1", .port = 0},
      accounts,
      worlds,
      supported_versions.value(),
      live_options.value());

  if (!service.start()) {
    std::cerr << "failed to start MariaDB-backed login smoke transport\n";
    return EXIT_FAILURE;
  }

  const auto exchange = exchange_login_sequence(eq2::net::SocketEndpoint{
                                                   .address = "127.0.0.1",
                                                   .port = service.port(),
                                               },
                                               options.username,
                                               options.password,
                                               options.client_version,
                                               live_options.value());
  if (!exchange.has_value() || !wait_for_login_count(service, 1)) {
    service.stop();
    std::cerr << "MariaDB-backed login smoke did not complete\n";
    return EXIT_FAILURE;
  }

  service.stop();
  std::cout << "smoke-login-mariadb reply_code=" << static_cast<int>(exchange->reply.reply_code)
            << " account_id=" << exchange->reply.account_id
            << " world_list=" << (exchange->world_list_received ? "yes" : "no")
            << " worlds=" << exchange->world_count
            << " character_list=" << (exchange->character_list_received ? "yes" : "no")
            << " post_world_reply=" << (exchange->post_world_reply_received ? "yes" : "no")
            << '\n';
  if (!validate_expected_world_count(*exchange, options)) {
    print_expected_world_count_error(*exchange, options);
    return EXIT_FAILURE;
  }
  return exchange->reply.reply_code ==
                     static_cast<std::uint8_t>(eq2::login::LoginReplyCode::accepted) &&
                 exchange->world_list_received && exchange->character_list_received &&
                 exchange->post_world_reply_received
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}

auto run_server(const eq2::core::ConfigProvider& config, const AppOptions& options) -> int {
  auto runtime = eq2::core::load_runtime_config(config);
  if (!runtime.has_value()) {
    std::cerr << runtime.error().message << '\n';
    return EXIT_FAILURE;
  }

  auto db_config = load_database_config(config);
  if (auto error = validate_database_config(config, db_config)) {
    std::cerr << *error << '\n';
    return EXIT_FAILURE;
  }

  auto connection = std::make_shared<eq2::db::MariaDbConnection>(std::move(db_config));
  auto preflight = preflight_database(connection);
  if (!preflight.has_value()) {
    std::cerr << preflight.error().message << '\n';
    return EXIT_FAILURE;
  }
  auto schema = eq2::db::preflight_login_database_schema(*connection);
  if (!schema.has_value()) {
    std::cerr << schema.error().message << '\n';
    return EXIT_FAILURE;
  }

  eq2::db::SqlLoginAccountRepository accounts(connection);
  eq2::db::SqlWorldRegistrationRepository worlds(connection);
  auto login_config = eq2::login::load_login_server_config(config);
  auto live_options = load_live_login_options_from_database(
      config, *connection, options.client_version);
  if (!live_options.has_value()) {
    std::cerr << live_options.error().message << '\n';
    return EXIT_FAILURE;
  }
  auto supported_versions = load_supported_login_versions(
      config, *connection, options.client_version);
  if (!supported_versions.has_value()) {
    std::cerr << supported_versions.error().message << '\n';
    return EXIT_FAILURE;
  }
  eq2::login::LiveLoginService service(
      login_config, accounts, worlds, supported_versions.value(), live_options.value());

  if (!service.start()) {
    std::cerr << "failed to start source2 login server on "
              << login_config.address << ':' << login_config.port << '\n';
    return EXIT_FAILURE;
  }

  std::signal(SIGINT, request_stop);
  std::signal(SIGTERM, request_stop);

  eq2::core::ConsoleLogSink log;
  eq2::core::log(log, eq2::core::LogLevel::info, "login", "source2 login server started");
  std::cout << "eq2_login_server source2 serving UDP/TCP "
            << login_config.address << ':' << service.port()
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
  eq2::core::log(log, eq2::core::LogLevel::info, "login", "source2 login server stopped");
  return EXIT_SUCCESS;
}

auto run_config_validation(const eq2::core::ConfigProvider& config) -> int {
  auto runtime = eq2::core::load_runtime_config(config);
  if (!runtime.has_value()) {
    std::cerr << runtime.error().message << '\n';
    return EXIT_FAILURE;
  }
  auto opcode_source = validate_login_opcode_source(config);
  if (!opcode_source.has_value()) {
    std::cerr << opcode_source.error().message << '\n';
    return EXIT_FAILURE;
  }
  const auto source_value = config.get("login.opcode_source").value_or("config");
  auto live_options = load_live_login_options(config, source_value != "database");
  if (!live_options.has_value()) {
    std::cerr << live_options.error().message << '\n';
    return EXIT_FAILURE;
  }

  eq2::core::ConsoleLogSink log;
  eq2::core::log(log, eq2::core::LogLevel::info, "login",
                 "source2 login composition root validated");

  std::cout << "eq2_login_server source2 wiring "
            << "version=" << eq2::core::kSource2Version << ' '
            << runtime.value().login_listen.address << ':'
            << runtime.value().login_listen.port << '\n';
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

  if (options.value().command == Command::smoke_login_live) {
    return run_live_smoke(options.value());
  }

  auto app_options = std::move(options.value());
  auto config = load_app_config(app_options);
  if (!config.has_value()) {
    std::cerr << config.error().message << '\n';
    return EXIT_FAILURE;
  }
  auto configured_options = apply_configured_app_options(app_options, config.value());
  if (!configured_options.has_value()) {
    std::cerr << configured_options.error().message << '\n';
    return EXIT_FAILURE;
  }

  switch (app_options.command) {
    case Command::validate_config:
      return run_config_validation(config.value());
    case Command::serve:
      return run_server(config.value(), app_options);
    case Command::check_login_db:
      return run_login_db_check(config.value(), app_options);
    case Command::smoke_login_mariadb:
      return run_mariadb_smoke(config.value(), app_options);
    case Command::probe_login:
      return run_probe_login(config.value(), app_options);
    case Command::smoke_login_live:
    case Command::help:
      break;
  }

  return EXIT_FAILURE;
}

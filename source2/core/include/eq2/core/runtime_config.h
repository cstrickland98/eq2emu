#pragma once

#include <charconv>
#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <eq2/core/config.h>
#include <eq2/core/result.h>

namespace eq2::core {

enum class ConfigSeverity {
  warning,
  error,
};

struct ConfigIssue {
  ConfigSeverity severity = ConfigSeverity::error;
  std::string key;
  std::string message;
};

class ConfigValidation {
 public:
  void warn(std::string key, std::string message) {
    issues_.push_back(ConfigIssue{
        .severity = ConfigSeverity::warning,
        .key = std::move(key),
        .message = std::move(message),
    });
  }

  void error(std::string key, std::string message) {
    issues_.push_back(ConfigIssue{
        .severity = ConfigSeverity::error,
        .key = std::move(key),
        .message = std::move(message),
    });
  }

  [[nodiscard]] auto ok() const -> bool {
    for (const auto& issue : issues_) {
      if (issue.severity == ConfigSeverity::error) {
        return false;
      }
    }

    return true;
  }

  [[nodiscard]] auto issues() const -> const std::vector<ConfigIssue>& {
    return issues_;
  }

 private:
  std::vector<ConfigIssue> issues_;
};

struct EndpointConfig {
  std::string address = "0.0.0.0";
  std::uint16_t port = 0;
};

struct LoggingConfig {
  std::string level = "info";
  bool console = true;
};

struct RuntimeConfig {
  EndpointConfig login_listen{.address = "0.0.0.0", .port = 9100};
  EndpointConfig world_listen{.address = "0.0.0.0", .port = 9101};
  EndpointConfig login_remote{.address = "127.0.0.1", .port = 9100};
  bool account_creation_allowed = false;
  LoggingConfig logging;
};

namespace detail {

inline auto trim(std::string_view value) -> std::string_view {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t' ||
                            value.front() == '\r' || value.front() == '\n')) {
    value.remove_prefix(1);
  }
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t' ||
                            value.back() == '\r' || value.back() == '\n')) {
    value.remove_suffix(1);
  }
  return value;
}

inline auto parse_u16(std::string_view value) -> std::optional<std::uint16_t> {
  auto result = 0U;
  const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), result);
  if (ec != std::errc{} || ptr != value.data() + value.size() || result > 0xffffU) {
    return std::nullopt;
  }

  return static_cast<std::uint16_t>(result);
}

}  // namespace detail

inline auto validate_runtime_config(const ConfigProvider& config) -> ConfigValidation {
  ConfigValidation validation;
  const auto require_port = [&config, &validation](std::string_view key) {
    const auto value = config.get(key);
    if (!value.has_value()) {
      validation.warn(std::string(key), "missing key; default will be used");
      return;
    }

    if (!detail::parse_u16(*value).has_value()) {
      validation.error(std::string(key), "expected TCP/UDP port from 0 to 65535");
    }
  };

  require_port("login.port");
  require_port("world.port");
  require_port("login.remote_port");

  for (const auto key : {"login.address", "world.address", "login.remote_address"}) {
    if (!config.get(key).has_value()) {
      validation.warn(key, "missing key; default will be used");
    }
  }

  const auto log_level = config.get("logging.level");
  if (log_level.has_value() && *log_level != "trace" && *log_level != "debug" &&
      *log_level != "info" && *log_level != "warning" && *log_level != "error" &&
      *log_level != "critical") {
    validation.error("logging.level", "unknown logging level");
  }

  return validation;
}

inline auto load_runtime_config(const ConfigProvider& config) -> Result<RuntimeConfig> {
  auto validation = validate_runtime_config(config);
  if (!validation.ok()) {
    std::ostringstream message;
    message << "runtime config validation failed";
    for (const auto& issue : validation.issues()) {
      if (issue.severity == ConfigSeverity::error) {
        message << "; " << issue.key << ": " << issue.message;
      }
    }
    return Result<RuntimeConfig>::failure(Error{
        .code = ErrorCode::invalid_argument,
        .message = message.str(),
    });
  }

  const auto port_or = [&config](std::string_view key, std::uint16_t fallback) {
    const auto value = config.get(key);
    if (!value.has_value()) {
      return fallback;
    }
    return detail::parse_u16(*value).value_or(fallback);
  };
  const auto bool_or = [&config](std::string_view key, bool fallback) {
    const auto value = config.get(key);
    if (!value.has_value()) {
      return fallback;
    }
    return *value == "1" || *value == "true" || *value == "yes" || *value == "on";
  };

  return Result<RuntimeConfig>::success(RuntimeConfig{
      .login_listen =
          EndpointConfig{
              .address = config.get("login.address").value_or("0.0.0.0"),
              .port = port_or("login.port", 9100),
          },
      .world_listen =
          EndpointConfig{
              .address = config.get("world.address").value_or("0.0.0.0"),
              .port = port_or("world.port", 9101),
          },
      .login_remote =
          EndpointConfig{
              .address = config.get("login.remote_address").value_or("127.0.0.1"),
              .port = port_or("login.remote_port", 9100),
          },
      .account_creation_allowed = bool_or("login.account_creation_allowed", false),
      .logging =
          LoggingConfig{
              .level = config.get("logging.level").value_or("info"),
              .console = bool_or("logging.console", true),
          },
  });
}

inline auto load_ini_config_file(const std::string& path) -> Result<MapConfig> {
  std::ifstream input(path);
  if (!input) {
    return Result<MapConfig>::failure(Error{
        .code = ErrorCode::not_found,
        .message = "config file not found: " + path,
    });
  }

  MapConfig config;
  std::string section;
  std::string line;
  while (std::getline(input, line)) {
    auto view = detail::trim(line);
    if (view.empty() || view.front() == '#' || view.front() == ';') {
      continue;
    }

    if (view.front() == '[' && view.back() == ']') {
      section = std::string(detail::trim(view.substr(1, view.size() - 2)));
      continue;
    }

    const auto separator = view.find('=');
    if (separator == std::string_view::npos) {
      continue;
    }

    auto key = std::string(detail::trim(view.substr(0, separator)));
    auto value = std::string(detail::trim(view.substr(separator + 1)));
    if (!section.empty()) {
      key = section + "." + key;
    }
    config.set(std::move(key), std::move(value));
  }

  return Result<MapConfig>::success(std::move(config));
}

}  // namespace eq2::core

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace eq2::core {

class ConfigProvider {
 public:
  virtual ~ConfigProvider() = default;

  [[nodiscard]] virtual auto get(std::string_view key) const -> std::optional<std::string> = 0;
};

class MapConfig final : public ConfigProvider {
 public:
  void set(std::string key, std::string value) {
    values_[std::move(key)] = std::move(value);
  }

  [[nodiscard]] auto get(std::string_view key) const -> std::optional<std::string> override {
    const auto value = values_.find(std::string(key));
    if (value == values_.end()) {
      return std::nullopt;
    }

    return value->second;
  }

  [[nodiscard]] auto get_or(std::string_view key, std::string fallback) const -> std::string {
    auto value = get(key);
    if (!value) {
      return fallback;
    }

    return *value;
  }

  [[nodiscard]] auto get_bool(std::string_view key, bool fallback = false) const -> bool {
    auto value = get(key);
    if (!value) {
      return fallback;
    }

    return *value == "1" || *value == "true" || *value == "yes" || *value == "on";
  }

 private:
  std::unordered_map<std::string, std::string> values_;
};

}  // namespace eq2::core

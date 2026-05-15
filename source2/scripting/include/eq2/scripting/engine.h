#pragma once

#include <cstdint>
#include <exception>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <eq2/core/log.h>
#include <eq2/core/result.h>

namespace eq2::scripting {

enum class ScriptCategory {
  item,
  quest,
  spell,
  spawn,
  zone,
  player,
  region,
};

enum class MutationTarget {
  world,
  zone,
  player,
};

struct ScriptId {
  ScriptCategory category = ScriptCategory::zone;
  std::string name;

  friend auto operator<(const ScriptId& lhs, const ScriptId& rhs) -> bool {
    if (lhs.category != rhs.category) {
      return lhs.category < rhs.category;
    }

    return lhs.name < rhs.name;
  }
};

struct ScriptEvent {
  ScriptCategory category = ScriptCategory::zone;
  std::string function;
  std::int32_t actor_id = 0;
  std::int32_t target_id = 0;
  struct Position {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float heading = 0.0F;
  };
  std::optional<Position> zone_safe_location;
};

struct ScriptMutation {
  MutationTarget target = MutationTarget::zone;
  std::string command;
  std::vector<std::int32_t> ids;
};

class OwnerCommandSink {
 public:
  virtual ~OwnerCommandSink() = default;

  virtual auto post_script_mutation(const ScriptMutation& mutation) -> bool = 0;
};

class ScriptContext {
 public:
  ScriptContext(ScriptEvent event, OwnerCommandSink& owner_sink)
      : event_(std::move(event)), owner_sink_(owner_sink) {}

  [[nodiscard]] auto event() const -> const ScriptEvent& {
    return event_;
  }

  auto post_mutation(ScriptMutation mutation) -> bool {
    return owner_sink_.post_script_mutation(mutation);
  }

 private:
  ScriptEvent event_;
  OwnerCommandSink& owner_sink_;
};

struct LoadedScript {
  ScriptId id;
  std::string source;
  std::uint64_t generation = 0;
};

class ScriptBackend {
 public:
  virtual ~ScriptBackend() = default;

  virtual auto call(const LoadedScript& script, ScriptContext& context)
      -> eq2::core::Result<void> = 0;
};

class ScriptEngine {
 public:
  ScriptEngine(ScriptBackend& backend, eq2::core::LogSink& log) : backend_(backend), log_(log) {}

  auto load_script(ScriptId id, std::string source) -> LoadedScript {
    const auto next_generation = next_generation_++;
    auto loaded = LoadedScript{
        .id = std::move(id),
        .source = std::move(source),
        .generation = next_generation,
    };
    scripts_[loaded.id] = loaded;
    return loaded;
  }

  auto reload_script(ScriptId id, std::string source) -> LoadedScript {
    scripts_.erase(id);
    return load_script(std::move(id), std::move(source));
  }

  [[nodiscard]] auto find_script(const ScriptId& id) const -> std::optional<LoadedScript> {
    const auto iter = scripts_.find(id);
    if (iter == scripts_.end()) {
      return std::nullopt;
    }

    return iter->second;
  }

  auto call_event(const ScriptId& id, ScriptEvent event, OwnerCommandSink& owner_sink)
      -> eq2::core::Result<void> {
    const auto script = find_script(id);
    if (!script.has_value()) {
      eq2::core::log(log_, eq2::core::LogLevel::error, "scripting", "script not loaded");
      return eq2::core::Result<void>::failure(eq2::core::Error{
          .code = eq2::core::ErrorCode::not_found,
          .message = "script not loaded",
      });
    }

    try {
      ScriptContext context(std::move(event), owner_sink);
      auto result = backend_.call(*script, context);
      if (!result.has_value()) {
        eq2::core::log(log_, eq2::core::LogLevel::error, "scripting", result.error().message);
      }

      return result;
    } catch (const std::exception& error) {
      eq2::core::log(log_, eq2::core::LogLevel::error, "scripting", error.what());
      return eq2::core::Result<void>::failure(eq2::core::Error{
          .code = eq2::core::ErrorCode::unknown,
          .message = error.what(),
      });
    }
  }

 private:
  ScriptBackend& backend_;
  eq2::core::LogSink& log_;
  std::map<ScriptId, LoadedScript> scripts_;
  std::uint64_t next_generation_ = 1;
};

inline auto item_event(std::string function, std::int32_t player_id, std::int32_t item_id)
    -> ScriptEvent {
  return ScriptEvent{
      .category = ScriptCategory::item,
      .function = std::move(function),
      .actor_id = player_id,
      .target_id = item_id,
  };
}

inline auto quest_event(std::string function, std::int32_t player_id, std::int32_t quest_id)
    -> ScriptEvent {
  return ScriptEvent{
      .category = ScriptCategory::quest,
      .function = std::move(function),
      .actor_id = player_id,
      .target_id = quest_id,
  };
}

inline auto spell_event(std::string function, std::int32_t caster_id, std::int32_t target_id)
    -> ScriptEvent {
  return ScriptEvent{
      .category = ScriptCategory::spell,
      .function = std::move(function),
      .actor_id = caster_id,
      .target_id = target_id,
  };
}

inline auto spawn_event(std::string function, std::int32_t spawn_id, std::int32_t actor_id = 0)
    -> ScriptEvent {
  return ScriptEvent{
      .category = ScriptCategory::spawn,
      .function = std::move(function),
      .actor_id = actor_id,
      .target_id = spawn_id,
  };
}

inline auto zone_event(std::string function,
                       std::int32_t zone_id,
                       std::int32_t actor_id = 0,
                       std::optional<ScriptEvent::Position> zone_safe_location = std::nullopt)
    -> ScriptEvent {
  return ScriptEvent{
      .category = ScriptCategory::zone,
      .function = std::move(function),
      .actor_id = actor_id,
      .target_id = zone_id,
      .zone_safe_location = zone_safe_location,
  };
}

inline auto player_event(std::string function, std::int32_t player_id) -> ScriptEvent {
  return ScriptEvent{
      .category = ScriptCategory::player,
      .function = std::move(function),
      .actor_id = player_id,
  };
}

inline auto region_event(std::string function, std::int32_t region_id, std::int32_t actor_id)
    -> ScriptEvent {
  return ScriptEvent{
      .category = ScriptCategory::region,
      .function = std::move(function),
      .actor_id = actor_id,
      .target_id = region_id,
  };
}

}  // namespace eq2::scripting

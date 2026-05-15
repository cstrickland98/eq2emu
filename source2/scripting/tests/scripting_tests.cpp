#include <eq2/scripting/engine.h>
#include <eq2/scripting/lua_backend.h>
#include <eq2/scripting/script_loader.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

auto failures = 0;

void require(bool condition, std::string_view message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

template <typename T, typename U>
void require_eq(const T& actual, const U& expected, std::string_view message) {
  if (!(actual == expected)) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

class CapturingLogSink final : public eq2::core::LogSink {
 public:
  void write(const eq2::core::LogRecord& record) override {
    records.push_back(record);
  }

  std::vector<eq2::core::LogRecord> records;
};

class CapturingCommandSink final : public eq2::scripting::OwnerCommandSink {
 public:
  auto post_script_mutation(const eq2::scripting::ScriptMutation& mutation) -> bool override {
    mutations.push_back(mutation);
    return true;
  }

  std::vector<eq2::scripting::ScriptMutation> mutations;
};

class PostingBackend final : public eq2::scripting::ScriptBackend {
 public:
  auto call(const eq2::scripting::LoadedScript& script,
            eq2::scripting::ScriptContext& context) -> eq2::core::Result<void> override {
    called_generation = script.generation;
    called_event = context.event();
    context.post_mutation(eq2::scripting::ScriptMutation{
        .target = eq2::scripting::MutationTarget::zone,
        .command = "move_spawn",
        .ids = {context.event().target_id},
    });
    return eq2::core::Result<void>::success();
  }

  std::uint64_t called_generation = 0;
  eq2::scripting::ScriptEvent called_event;
};

class FailingBackend final : public eq2::scripting::ScriptBackend {
 public:
  auto call(const eq2::scripting::LoadedScript&,
            eq2::scripting::ScriptContext&) -> eq2::core::Result<void> override {
    return eq2::core::Result<void>::failure(eq2::core::Error{
        .code = eq2::core::ErrorCode::parse_error,
        .message = "script call failed",
    });
  }
};

class ThrowingBackend final : public eq2::scripting::ScriptBackend {
 public:
  auto call(const eq2::scripting::LoadedScript&,
            eq2::scripting::ScriptContext&) -> eq2::core::Result<void> override {
    throw std::runtime_error("lua panic");
  }
};

void script_lifetime_loads_and_reloads_generations() {
  PostingBackend backend;
  CapturingLogSink log;
  eq2::scripting::ScriptEngine engine(backend, log);
  const auto id = eq2::scripting::ScriptId{
      .category = eq2::scripting::ScriptCategory::zone,
      .name = "qeynos",
  };

  const auto first = engine.load_script(id, "function player_entry() end");
  const auto second = engine.reload_script(id, "function player_entry() return true end");
  const auto loaded = engine.find_script(id);

  require_eq(first.generation, static_cast<std::uint64_t>(1), "first load receives generation one");
  require_eq(second.generation, static_cast<std::uint64_t>(2), "reload receives a new generation");
  require(loaded.has_value(), "reloaded script is findable");
  require_eq(loaded->generation, second.generation, "loaded script tracks latest generation");
}

void script_calls_post_mutations_to_owner_sink() {
  PostingBackend backend;
  CapturingLogSink log;
  CapturingCommandSink sink;
  eq2::scripting::ScriptEngine engine(backend, log);
  const auto id = eq2::scripting::ScriptId{
      .category = eq2::scripting::ScriptCategory::spawn,
      .name = "guard",
  };
  engine.load_script(id, "function hailed() end");

  const auto result = engine.call_event(id, eq2::scripting::spawn_event("hailed", 2002, 42), sink);

  require(result.has_value(), "script call succeeds");
  require_eq(backend.called_event.category, eq2::scripting::ScriptCategory::spawn,
             "spawn event category is explicit");
  require_eq(backend.called_event.function, std::string_view("hailed"),
             "script event carries function name");
  require_eq(sink.mutations.size(), static_cast<std::size_t>(1),
             "script mutation is posted to owner sink");
  require_eq(sink.mutations.front().command, std::string_view("move_spawn"),
             "script mutation carries command name");
  require_eq(sink.mutations.front().ids.front(), 2002,
             "script mutation targets the event spawn");
}

void script_failures_are_isolated_and_logged() {
  FailingBackend backend;
  CapturingLogSink log;
  CapturingCommandSink sink;
  eq2::scripting::ScriptEngine engine(backend, log);
  const auto id = eq2::scripting::ScriptId{
      .category = eq2::scripting::ScriptCategory::quest,
      .name = "quest_1",
  };
  engine.load_script(id, "function Accepted() end");

  const auto result = engine.call_event(id, eq2::scripting::quest_event("Accepted", 42, 1), sink);

  require(!result.has_value(), "failed script call returns an error result");
  require_eq(result.error().code, eq2::core::ErrorCode::parse_error,
             "failed script preserves backend error code");
  require_eq(log.records.size(), static_cast<std::size_t>(1), "failed script is logged");
  require_eq(log.records.front().level, eq2::core::LogLevel::error,
             "failed script logs an error");
}

void script_exceptions_are_captured_as_failures() {
  ThrowingBackend backend;
  CapturingLogSink log;
  CapturingCommandSink sink;
  eq2::scripting::ScriptEngine engine(backend, log);
  const auto id = eq2::scripting::ScriptId{
      .category = eq2::scripting::ScriptCategory::spell,
      .name = "spell_1",
  };
  engine.load_script(id, "function cast() error('bad') end");

  const auto result = engine.call_event(id, eq2::scripting::spell_event("cast", 10, 11), sink);

  require(!result.has_value(), "throwing script call returns failure");
  require_eq(result.error().code, eq2::core::ErrorCode::unknown,
             "throwing script maps to isolated error");
  require_eq(log.records.front().message, std::string_view("lua panic"),
             "throwing script logs exception message");
}

void explicit_event_helpers_cover_script_categories() {
  require_eq(eq2::scripting::item_event("used", 1, 2).category,
             eq2::scripting::ScriptCategory::item, "item event helper sets category");
  require_eq(eq2::scripting::quest_event("Accepted", 1, 2).category,
             eq2::scripting::ScriptCategory::quest, "quest event helper sets category");
  require_eq(eq2::scripting::spell_event("cast", 1, 2).category,
             eq2::scripting::ScriptCategory::spell, "spell event helper sets category");
  require_eq(eq2::scripting::spawn_event("hailed", 2, 1).category,
             eq2::scripting::ScriptCategory::spawn, "spawn event helper sets category");
  require_eq(eq2::scripting::zone_event("player_entry", 2, 1).category,
             eq2::scripting::ScriptCategory::zone, "zone event helper sets category");
  require_eq(eq2::scripting::player_event("on_level_up", 1).category,
             eq2::scripting::ScriptCategory::player, "player event helper sets category");
  require_eq(eq2::scripting::region_event("enter_location", 2, 1).category,
             eq2::scripting::ScriptCategory::region, "region event helper sets category");
}

void script_loader_resolves_paths_and_reads_sources() {
  namespace fs = std::filesystem;

  const auto root = fs::temp_directory_path() / "eq2emu_source2_script_loader_test";
  fs::remove_all(root);
  fs::create_directories(root / "zone");

  const auto id = eq2::scripting::ScriptId{
      .category = eq2::scripting::ScriptCategory::zone,
      .name = "qeynos",
  };
  const auto path = eq2::scripting::script_path_for(root, id);
  {
    std::ofstream(path) << "function enter() end";
  }

  require_eq(path.parent_path().filename().string(), std::string_view("zone"),
             "script loader maps category to directory");
  const auto source = eq2::scripting::read_script_source(path);
  require(source.has_value(), "script loader reads Lua source from path");
  require_eq(source.value(), std::string_view("function enter() end"),
             "script loader preserves source bytes");

  fs::remove_all(root);
}

void lua_backend_reports_clean_unavailable_when_runtime_is_missing() {
  eq2::scripting::LuaBackend backend;
  if (backend.available()) {
    return;
  }

  CapturingCommandSink sink;
  const auto script = eq2::scripting::LoadedScript{
      .id =
          eq2::scripting::ScriptId{
              .category = eq2::scripting::ScriptCategory::zone,
              .name = "missing_runtime",
          },
      .source = "function enter() end",
      .generation = 1,
  };
  eq2::scripting::ScriptContext context(eq2::scripting::zone_event("enter", 10), sink);
  const auto result = backend.call(script, context);

  require(!result.has_value(), "Lua backend reports failure when runtime is unavailable");
  require_eq(result.error().code, eq2::core::ErrorCode::unavailable,
             "Lua backend uses unavailable error when runtime is missing");
}

void real_lua_backend_loads_calls_reloads_and_posts_owner_mutations() {
  eq2::scripting::LuaBackend backend;
  if (!backend.available()) {
    return;
  }

  CapturingLogSink log;
  CapturingCommandSink sink;
  eq2::scripting::ScriptEngine engine(backend, log);
  const auto id = eq2::scripting::ScriptId{
      .category = eq2::scripting::ScriptCategory::zone,
      .name = "qeynos",
  };

  engine.load_script(id, "function enter() PostZoneMutation(EventName(), TargetId()) end");
  auto result = engine.call_event(id, eq2::scripting::zone_event("enter", 10, 42), sink);
  require(result.has_value(), "real Lua backend calls loaded Lua function");
  require_eq(sink.mutations.back().command, std::string_view("enter"),
             "real Lua backend exposes EventName API");
  require_eq(sink.mutations.back().ids.back(), 10, "real Lua backend exposes TargetId API");

  engine.reload_script(id, "function enter() PostZoneMutation('actor', ActorId()) end");
  result = engine.call_event(id, eq2::scripting::zone_event("enter", 10, 42), sink);
  require(result.has_value(), "real Lua backend calls reloaded Lua function");
  require_eq(sink.mutations.back().command, std::string_view("actor"),
             "real Lua backend uses reloaded script source");
  require_eq(sink.mutations.back().ids.back(), 42, "real Lua backend exposes ActorId API");
}

void real_lua_backend_isolates_lua_errors_and_missing_functions() {
  eq2::scripting::LuaBackend backend;
  if (!backend.available()) {
    return;
  }

  CapturingLogSink log;
  CapturingCommandSink sink;
  eq2::scripting::ScriptEngine engine(backend, log);
  const auto id = eq2::scripting::ScriptId{
      .category = eq2::scripting::ScriptCategory::spawn,
      .name = "guard",
  };

  engine.load_script(id, "function fail() error('boom') end");
  auto result = engine.call_event(id, eq2::scripting::spawn_event("fail", 2002, 42), sink);
  require(!result.has_value(), "real Lua backend isolates Lua runtime errors");
  require_eq(result.error().code, eq2::core::ErrorCode::parse_error,
             "real Lua backend maps Lua errors to parse failures");
  require(!log.records.empty(), "real Lua backend errors are logged by script engine");

  result = engine.call_event(id, eq2::scripting::spawn_event("missing", 2002, 42), sink);
  require(!result.has_value(), "real Lua backend reports missing function");
  require_eq(result.error().code, eq2::core::ErrorCode::not_found,
             "real Lua backend maps missing function to not_found");
}

void real_lua_backend_smokes_supported_script_categories() {
  eq2::scripting::LuaBackend backend;
  if (!backend.available()) {
    return;
  }

  CapturingLogSink log;
  CapturingCommandSink sink;
  eq2::scripting::ScriptEngine engine(backend, log);

  const std::vector<eq2::scripting::ScriptEvent> events = {
      eq2::scripting::item_event("handle", 1, 100),
      eq2::scripting::quest_event("handle", 1, 101),
      eq2::scripting::spell_event("handle", 1, 102),
      eq2::scripting::spawn_event("handle", 103, 1),
      eq2::scripting::zone_event("handle", 104, 1),
      eq2::scripting::player_event("handle", 105),
      eq2::scripting::region_event("handle", 106, 1),
  };

  for (const auto& event : events) {
    const auto id = eq2::scripting::ScriptId{
        .category = event.category,
        .name = "smoke",
    };
    engine.load_script(id, "function handle() PostZoneMutation('smoke', TargetId()) end");
    const auto result = engine.call_event(id, event, sink);
    require(result.has_value(), "real Lua backend handles supported event category");
  }

  require_eq(sink.mutations.size(), static_cast<std::size_t>(events.size()),
             "real Lua backend smoke posts one owner mutation per category");
}

}  // namespace

int main() {
  script_lifetime_loads_and_reloads_generations();
  script_calls_post_mutations_to_owner_sink();
  script_failures_are_isolated_and_logged();
  script_exceptions_are_captured_as_failures();
  explicit_event_helpers_cover_script_categories();
  script_loader_resolves_paths_and_reads_sources();
  lua_backend_reports_clean_unavailable_when_runtime_is_missing();
  real_lua_backend_loads_calls_reloads_and_posts_owner_mutations();
  real_lua_backend_isolates_lua_errors_and_missing_functions();
  real_lua_backend_smokes_supported_script_categories();

  if (failures != 0) {
    std::cerr << failures << " scripting assertion(s) failed\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

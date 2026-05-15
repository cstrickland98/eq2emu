#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <eq2/scripting/engine.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace eq2::scripting {

namespace detail {

struct lua_State;
using LuaInteger = long long;
using LuaCFunction = int (*)(lua_State*);

inline constexpr auto kLuaOk = 0;
inline constexpr auto kLuaTypeFunction = 6;

struct LuaApi {
#ifdef _WIN32
  using NewStateFn = lua_State* (*)();
  using CloseFn = void (*)(lua_State*);
  using OpenLibsFn = void (*)(lua_State*);
  using LoadBufferFn = int (*)(lua_State*, const char*, std::size_t, const char*, const char*);
  using PCallFn = int (*)(lua_State*, int, int, int, std::intptr_t, void*);
  using GetGlobalFn = int (*)(lua_State*, const char*);
  using SetGlobalFn = void (*)(lua_State*, const char*);
  using TypeFn = int (*)(lua_State*, int);
  using SetTopFn = void (*)(lua_State*, int);
  using ToStringFn = const char* (*)(lua_State*, int, std::size_t*);
  using ToIntegerFn = LuaInteger (*)(lua_State*, int, int*);
  using PushCClosureFn = void (*)(lua_State*, LuaCFunction, int);
  using PushIntegerFn = void (*)(lua_State*, LuaInteger);
  using PushStringFn = const char* (*)(lua_State*, const char*);
  using PushBooleanFn = void (*)(lua_State*, int);

  ~LuaApi() {
    if (module != nullptr) {
      FreeLibrary(module);
    }
  }

  LuaApi() = default;
  LuaApi(const LuaApi&) = delete;
  auto operator=(const LuaApi&) -> LuaApi& = delete;

  auto load() -> bool {
    for (const auto& candidate : library_candidates()) {
      module = LoadLibraryA(candidate.c_str());
      if (module != nullptr) {
        library_path = candidate;
        break;
      }
    }

    if (module == nullptr) {
      error = "Lua 5.3 runtime DLL was not found";
      return false;
    }

    if (!resolve(new_state, "luaL_newstate") || !resolve(close, "lua_close") ||
        !resolve(open_libs, "luaL_openlibs") ||
        !resolve(load_buffer, "luaL_loadbufferx") ||
        !resolve(pcall, "lua_pcallk") || !resolve(get_global, "lua_getglobal") ||
        !resolve(set_global, "lua_setglobal") || !resolve(type, "lua_type") ||
        !resolve(set_top, "lua_settop") || !resolve(to_string, "lua_tolstring") ||
        !resolve(to_integer, "lua_tointegerx") ||
        !resolve(push_cclosure, "lua_pushcclosure") ||
        !resolve(push_integer, "lua_pushinteger") ||
        !resolve(push_string, "lua_pushstring") ||
        !resolve(push_boolean, "lua_pushboolean")) {
      FreeLibrary(module);
      module = nullptr;
      return false;
    }

    return true;
  }

  template <typename T>
  auto resolve(T& target, const char* name) -> bool {
    auto* address = GetProcAddress(module, name);
    if (address == nullptr) {
      error = std::string("Lua runtime is missing symbol: ") + name;
      return false;
    }

    target = reinterpret_cast<T>(address);
    return true;
  }

  static auto env_var(const char* name) -> std::optional<std::string> {
    const auto required = GetEnvironmentVariableA(name, nullptr, 0);
    if (required == 0) {
      return std::nullopt;
    }

    std::string value(required, '\0');
    const auto written = GetEnvironmentVariableA(name, value.data(), required);
    if (written == 0 || written >= required) {
      return std::nullopt;
    }

    value.resize(written);
    return value;
  }

  static auto library_candidates() -> std::vector<std::string> {
    auto candidates = std::vector<std::string>{};
    if (auto explicit_path = env_var("EQ2_LUA_DLL")) {
      candidates.push_back(*explicit_path);
    }

    candidates.push_back("lua53-64.dll");
    candidates.push_back("lua53.dll");
    candidates.push_back("lua.dll");
    candidates.push_back("C:\\Program Files\\Cheat Engine\\lua53-64.dll");
    candidates.push_back("C:\\Program Files\\Cheat Engine 7.4\\lua53-64.dll");
    return candidates;
  }

  HMODULE module = nullptr;
  std::string library_path;
  std::string error;
  NewStateFn new_state = nullptr;
  CloseFn close = nullptr;
  OpenLibsFn open_libs = nullptr;
  LoadBufferFn load_buffer = nullptr;
  PCallFn pcall = nullptr;
  GetGlobalFn get_global = nullptr;
  SetGlobalFn set_global = nullptr;
  TypeFn type = nullptr;
  SetTopFn set_top = nullptr;
  ToStringFn to_string = nullptr;
  ToIntegerFn to_integer = nullptr;
  PushCClosureFn push_cclosure = nullptr;
  PushIntegerFn push_integer = nullptr;
  PushStringFn push_string = nullptr;
  PushBooleanFn push_boolean = nullptr;
#else
  auto load() -> bool {
    error = "Lua runtime loading is only implemented for Windows in source2";
    return false;
  }

  std::string library_path;
  std::string error;
#endif
};

inline thread_local LuaApi* current_lua_api = nullptr;
inline thread_local ScriptContext* current_script_context = nullptr;

inline auto lua_string_arg(lua_State* state, int index) -> std::optional<std::string> {
#ifdef _WIN32
  if (current_lua_api == nullptr) {
    return std::nullopt;
  }

  auto size = std::size_t{0};
  const auto* value = current_lua_api->to_string(state, index, &size);
  if (value == nullptr) {
    return std::nullopt;
  }

  return std::string(value, size);
#else
  (void)state;
  (void)index;
  return std::nullopt;
#endif
}

inline auto lua_integer_arg(lua_State* state, int index, std::int32_t fallback) -> std::int32_t {
#ifdef _WIN32
  if (current_lua_api == nullptr) {
    return fallback;
  }

  auto ok = 0;
  const auto value = current_lua_api->to_integer(state, index, &ok);
  return ok == 0 ? fallback : static_cast<std::int32_t>(value);
#else
  (void)state;
  (void)index;
  return fallback;
#endif
}

inline auto lua_post_zone_mutation(lua_State* state) -> int {
#ifdef _WIN32
  if (current_lua_api == nullptr || current_script_context == nullptr) {
    return 0;
  }

  const auto command = lua_string_arg(state, 1).value_or("script_mutation");
  const auto id = lua_integer_arg(state, 2, current_script_context->event().target_id);
  const auto posted = current_script_context->post_mutation(ScriptMutation{
      .target = MutationTarget::zone,
      .command = command,
      .ids = {id},
  });
  current_lua_api->push_boolean(state, posted ? 1 : 0);
  return 1;
#else
  (void)state;
  return 0;
#endif
}

inline auto lua_actor_id(lua_State* state) -> int {
#ifdef _WIN32
  if (current_lua_api != nullptr && current_script_context != nullptr) {
    current_lua_api->push_integer(state, current_script_context->event().actor_id);
    return 1;
  }
#else
  (void)state;
#endif
  return 0;
}

inline auto lua_target_id(lua_State* state) -> int {
#ifdef _WIN32
  if (current_lua_api != nullptr && current_script_context != nullptr) {
    current_lua_api->push_integer(state, current_script_context->event().target_id);
    return 1;
  }
#else
  (void)state;
#endif
  return 0;
}

inline auto lua_event_name(lua_State* state) -> int {
#ifdef _WIN32
  if (current_lua_api != nullptr && current_script_context != nullptr) {
    current_lua_api->push_string(state, current_script_context->event().function.c_str());
    return 1;
  }
#else
  (void)state;
#endif
  return 0;
}

class LuaThreadContext final {
 public:
  LuaThreadContext(LuaApi& api, ScriptContext& context)
      : previous_api_(current_lua_api), previous_context_(current_script_context) {
    current_lua_api = &api;
    current_script_context = &context;
  }

  ~LuaThreadContext() {
    current_lua_api = previous_api_;
    current_script_context = previous_context_;
  }

 private:
  LuaApi* previous_api_ = nullptr;
  ScriptContext* previous_context_ = nullptr;
};

}  // namespace detail

class LuaBackend final : public ScriptBackend {
 public:
  LuaBackend() : api_(std::make_shared<detail::LuaApi>()) {
    available_ = api_->load();
  }

  [[nodiscard]] auto available() const -> bool {
    return available_;
  }

  [[nodiscard]] auto library_path() const -> const std::string& {
    return api_->library_path;
  }

  [[nodiscard]] auto load_error() const -> const std::string& {
    return api_->error;
  }

  auto call(const LoadedScript& script, ScriptContext& context) -> eq2::core::Result<void> override {
#ifdef _WIN32
    if (!available_) {
      return eq2::core::Result<void>::failure(eq2::core::Error{
          .code = eq2::core::ErrorCode::unavailable,
          .message = api_->error,
      });
    }

    auto* state = api_->new_state();
    if (state == nullptr) {
      return eq2::core::Result<void>::failure(eq2::core::Error{
          .code = eq2::core::ErrorCode::unavailable,
          .message = "failed to allocate Lua state",
      });
    }

    struct StateGuard {
      std::shared_ptr<detail::LuaApi> api;
      detail::lua_State* state = nullptr;
      ~StateGuard() {
        if (state != nullptr) {
          api->close(state);
        }
      }
    } guard{api_, state};

    detail::LuaThreadContext thread_context(*api_, context);
    api_->open_libs(state);
    register_source2_api(state);

    auto result = load_and_run_script(state, script);
    if (!result.has_value()) {
      return result;
    }

    api_->get_global(state, context.event().function.c_str());
    if (api_->type(state, -1) != detail::kLuaTypeFunction) {
      api_->set_top(state, 0);
      return eq2::core::Result<void>::failure(eq2::core::Error{
          .code = eq2::core::ErrorCode::not_found,
          .message = "Lua function not found: " + context.event().function,
      });
    }

    if (api_->pcall(state, 0, 0, 0, 0, nullptr) != detail::kLuaOk) {
      return lua_failure(state, eq2::core::ErrorCode::parse_error, "Lua call failed");
    }

    return eq2::core::Result<void>::success();
#else
    (void)script;
    (void)context;
    return eq2::core::Result<void>::failure(eq2::core::Error{
        .code = eq2::core::ErrorCode::unavailable,
        .message = "Lua backend unavailable on this platform",
    });
#endif
  }

 private:
#ifdef _WIN32
  void register_source2_api(detail::lua_State* state) {
    api_->push_cclosure(state, detail::lua_post_zone_mutation, 0);
    api_->set_global(state, "PostZoneMutation");
    api_->push_cclosure(state, detail::lua_actor_id, 0);
    api_->set_global(state, "ActorId");
    api_->push_cclosure(state, detail::lua_target_id, 0);
    api_->set_global(state, "TargetId");
    api_->push_cclosure(state, detail::lua_event_name, 0);
    api_->set_global(state, "EventName");
  }

  auto load_and_run_script(detail::lua_State* state, const LoadedScript& script)
      -> eq2::core::Result<void> {
    if (api_->load_buffer(state,
                          script.source.data(),
                          script.source.size(),
                          script.id.name.c_str(),
                          nullptr) != detail::kLuaOk) {
      return lua_failure(state, eq2::core::ErrorCode::parse_error, "Lua load failed");
    }

    if (api_->pcall(state, 0, 0, 0, 0, nullptr) != detail::kLuaOk) {
      return lua_failure(state, eq2::core::ErrorCode::parse_error, "Lua script initialization failed");
    }

    return eq2::core::Result<void>::success();
  }

  auto lua_failure(detail::lua_State* state,
                   eq2::core::ErrorCode code,
                   const std::string& prefix) -> eq2::core::Result<void> {
    auto size = std::size_t{0};
    const auto* message = api_->to_string(state, -1, &size);
    auto text = prefix;
    if (message != nullptr) {
      text += ": ";
      text.append(message, size);
    }
    api_->set_top(state, 0);

    return eq2::core::Result<void>::failure(eq2::core::Error{
        .code = code,
        .message = std::move(text),
    });
  }
#endif

  std::shared_ptr<detail::LuaApi> api_;
  bool available_ = false;
};

}  // namespace eq2::scripting

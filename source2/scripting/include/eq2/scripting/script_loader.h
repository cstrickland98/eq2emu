#pragma once

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include <eq2/core/result.h>
#include <eq2/scripting/engine.h>

namespace eq2::scripting {

inline auto script_category_name(ScriptCategory category) -> std::string_view {
  switch (category) {
    case ScriptCategory::item:
      return "item";
    case ScriptCategory::quest:
      return "quest";
    case ScriptCategory::spell:
      return "spell";
    case ScriptCategory::spawn:
      return "spawn";
    case ScriptCategory::zone:
      return "zone";
    case ScriptCategory::player:
      return "player";
    case ScriptCategory::region:
      return "region";
  }

  return "unknown";
}

inline auto script_path_for(const std::filesystem::path& root, const ScriptId& id)
    -> std::filesystem::path {
  return root / std::string(script_category_name(id.category)) / (id.name + ".lua");
}

inline auto read_script_source(const std::filesystem::path& path)
    -> eq2::core::Result<std::string> {
  std::ifstream input(path);
  if (!input) {
    return eq2::core::Result<std::string>::failure(eq2::core::Error{
        .code = eq2::core::ErrorCode::not_found,
        .message = "script file not found: " + path.string(),
    });
  }

  return eq2::core::Result<std::string>::success(
      std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()));
}

}  // namespace eq2::scripting

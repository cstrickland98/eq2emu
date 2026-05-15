add_library(eq2_scripting INTERFACE)
add_library(eq2::scripting ALIAS eq2_scripting)

find_file(EQ2_LUA_RUNTIME_DLL
  NAMES lua53-64.dll lua53.dll lua.dll
  PATHS
    "$ENV{EQ2_LUA_DIR}"
    "C:/Program Files/Cheat Engine"
    "C:/Program Files/Cheat Engine 7.4"
  DOC "Optional Lua runtime DLL used by source2 runtime-loaded LuaBackend"
)

if(EQ2_LUA_RUNTIME_DLL)
  message(STATUS "source2 scripting Lua runtime discovered: ${EQ2_LUA_RUNTIME_DLL}")
else()
  message(STATUS "source2 scripting Lua runtime not found at configure time; LuaBackend can still use EQ2_LUA_DLL at runtime")
endif()

target_include_directories(eq2_scripting INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(eq2_scripting INTERFACE
  eq2::core
)

eq2_target_defaults(eq2_scripting)

if(BUILD_TESTING)
  add_executable(eq2_scripting_tests
    tests/scripting_tests.cpp
  )

  target_link_libraries(eq2_scripting_tests PRIVATE
    eq2::scripting
  )

  eq2_target_defaults(eq2_scripting_tests)

  add_test(NAME eq2_scripting_tests
    COMMAND eq2_scripting_tests
  )
endif()

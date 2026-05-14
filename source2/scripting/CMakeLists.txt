add_library(eq2_scripting INTERFACE)
add_library(eq2::scripting ALIAS eq2_scripting)

target_include_directories(eq2_scripting INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(eq2_scripting INTERFACE
  eq2::core
)

eq2_target_defaults(eq2_scripting)

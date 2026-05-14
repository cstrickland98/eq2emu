add_library(eq2_core INTERFACE)
add_library(eq2::core ALIAS eq2_core)

target_include_directories(eq2_core INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

eq2_target_defaults(eq2_core)

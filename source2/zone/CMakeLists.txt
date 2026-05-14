add_library(eq2_zone INTERFACE)
add_library(eq2::zone ALIAS eq2_zone)

target_include_directories(eq2_zone INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(eq2_zone INTERFACE
  eq2::core
  eq2::protocol
  eq2::db
  eq2::scripting
)

eq2_target_defaults(eq2_zone)

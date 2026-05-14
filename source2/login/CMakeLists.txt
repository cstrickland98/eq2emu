add_library(eq2_login INTERFACE)
add_library(eq2::login ALIAS eq2_login)

target_include_directories(eq2_login INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(eq2_login INTERFACE
  eq2::core
  eq2::protocol
  eq2::net
  eq2::db
)

eq2_target_defaults(eq2_login)

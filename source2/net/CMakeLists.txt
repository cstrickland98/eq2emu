add_library(eq2_net INTERFACE)
add_library(eq2::net ALIAS eq2_net)

target_include_directories(eq2_net INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(eq2_net INTERFACE
  eq2::core
  eq2::external::threads
)

eq2_target_defaults(eq2_net)

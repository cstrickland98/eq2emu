add_library(eq2_protocol INTERFACE)
add_library(eq2::protocol ALIAS eq2_protocol)

target_include_directories(eq2_protocol INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(eq2_protocol INTERFACE
  eq2::core
)

eq2_target_defaults(eq2_protocol)

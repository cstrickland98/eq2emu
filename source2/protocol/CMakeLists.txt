add_library(eq2_protocol INTERFACE)
add_library(eq2::protocol ALIAS eq2_protocol)

target_include_directories(eq2_protocol INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(eq2_protocol INTERFACE
  eq2::core
)

eq2_target_defaults(eq2_protocol)

if(BUILD_TESTING)
  add_executable(eq2_protocol_tests
    tests/protocol_tests.cpp
  )

  target_link_libraries(eq2_protocol_tests PRIVATE
    eq2::protocol
  )

  eq2_target_defaults(eq2_protocol_tests)

  add_test(NAME eq2_protocol_tests
    COMMAND eq2_protocol_tests
  )
endif()

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

if(BUILD_TESTING)
  add_executable(eq2_login_protocol_characterization_tests
    ../protocol/tests/protocol_characterization_tests.cpp
  )

  target_link_libraries(eq2_login_protocol_characterization_tests PRIVATE
    eq2::login
  )

  eq2_target_defaults(eq2_login_protocol_characterization_tests)

  add_test(NAME eq2_login_protocol_characterization_tests
    COMMAND eq2_login_protocol_characterization_tests
  )

  add_executable(eq2_login_characterization_tests
    tests/login_characterization_tests.cpp
  )

  target_link_libraries(eq2_login_characterization_tests PRIVATE
    eq2::login
  )

  eq2_target_defaults(eq2_login_characterization_tests)

  add_test(NAME eq2_login_characterization_tests
    COMMAND eq2_login_characterization_tests
  )

  add_executable(eq2_world_registration_characterization_tests
    tests/world_registration_characterization_tests.cpp
  )

  target_link_libraries(eq2_world_registration_characterization_tests PRIVATE
    eq2::login
  )

  eq2_target_defaults(eq2_world_registration_characterization_tests)

  add_test(NAME eq2_world_registration_characterization_tests
    COMMAND eq2_world_registration_characterization_tests
  )
endif()

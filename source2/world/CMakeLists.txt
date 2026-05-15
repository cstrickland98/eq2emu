add_library(eq2_world INTERFACE)
add_library(eq2::world ALIAS eq2_world)

target_include_directories(eq2_world INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(eq2_world INTERFACE
  eq2::core
  eq2::protocol
  eq2::net
  eq2::db
  eq2::zone
  eq2::scripting
)

eq2_target_defaults(eq2_world)

if(BUILD_TESTING)
  add_executable(eq2_world_session_tests
    tests/world_session_tests.cpp
  )

  target_link_libraries(eq2_world_session_tests PRIVATE
    eq2::world
  )

  eq2_target_defaults(eq2_world_session_tests)

  add_test(NAME eq2_world_session_tests
    COMMAND eq2_world_session_tests
  )

  add_executable(eq2_gameplay_systems_tests
    tests/gameplay_systems_tests.cpp
  )

  target_link_libraries(eq2_gameplay_systems_tests PRIVATE
    eq2::world
  )

  eq2_target_defaults(eq2_gameplay_systems_tests)

  add_test(NAME eq2_gameplay_systems_tests
    COMMAND eq2_gameplay_systems_tests
  )
endif()

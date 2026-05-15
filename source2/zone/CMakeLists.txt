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

if(BUILD_TESTING)
  add_executable(eq2_zone_runtime_tests
    tests/zone_runtime_tests.cpp
  )

  target_link_libraries(eq2_zone_runtime_tests PRIVATE
    eq2::zone
  )

  eq2_target_defaults(eq2_zone_runtime_tests)

  add_test(NAME eq2_zone_runtime_tests
    COMMAND eq2_zone_runtime_tests
  )
endif()

add_library(eq2_core INTERFACE)
add_library(eq2::core ALIAS eq2_core)

target_include_directories(eq2_core INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(eq2_core INTERFACE
  eq2::external::threads
)

eq2_target_defaults(eq2_core)

if(BUILD_TESTING)
  add_executable(eq2_core_tests
    tests/core_tests.cpp
  )

  target_link_libraries(eq2_core_tests PRIVATE
    eq2::core
  )

  eq2_target_defaults(eq2_core_tests)

  add_test(NAME eq2_core_tests
    COMMAND eq2_core_tests
  )
endif()

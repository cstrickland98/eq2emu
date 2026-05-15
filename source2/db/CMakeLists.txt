add_library(eq2_db INTERFACE)
add_library(eq2::db ALIAS eq2_db)

target_include_directories(eq2_db INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(eq2_db INTERFACE
  eq2::core
  eq2::external::threads
)

eq2_target_defaults(eq2_db)

if(BUILD_TESTING)
  add_executable(eq2_db_tests
    tests/db_tests.cpp
  )

  target_link_libraries(eq2_db_tests PRIVATE
    eq2::db
  )

  eq2_target_defaults(eq2_db_tests)

  add_test(NAME eq2_db_tests
    COMMAND eq2_db_tests
  )
endif()

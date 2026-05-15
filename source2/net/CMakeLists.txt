add_library(eq2_net INTERFACE)
add_library(eq2::net ALIAS eq2_net)

target_include_directories(eq2_net INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(eq2_net INTERFACE
  eq2::core
  eq2::external::threads
)

if(WIN32)
  target_link_libraries(eq2_net INTERFACE ws2_32)
endif()

eq2_target_defaults(eq2_net)

if(BUILD_TESTING)
  add_executable(eq2_net_tests
    tests/net_tests.cpp
  )

  target_link_libraries(eq2_net_tests PRIVATE
    eq2::net
  )

  eq2_target_defaults(eq2_net_tests)

  add_test(NAME eq2_net_tests
    COMMAND eq2_net_tests
  )
endif()

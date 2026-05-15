add_executable(eq2_login_server
  login_server/main.cpp
)

target_link_libraries(eq2_login_server PRIVATE
  eq2::login
)

eq2_target_defaults(eq2_login_server)

add_executable(eq2_world_server
  world_server/main.cpp
)

target_link_libraries(eq2_world_server PRIVATE
  eq2::world
  eq2::login
)

eq2_target_defaults(eq2_world_server)

if(BUILD_TESTING AND EQ2_SOURCE2_ENABLE_LIVE_SMOKE_TESTS)
  add_test(NAME eq2_login_live_smoke
    COMMAND eq2_login_server --smoke-login-live
  )

  add_test(NAME eq2_world_live_smoke
    COMMAND eq2_world_server --smoke-world-live
  )
endif()

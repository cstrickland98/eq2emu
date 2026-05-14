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
)

eq2_target_defaults(eq2_world_server)

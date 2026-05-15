add_executable(eq2_db_updater
  main.cpp
)

target_link_libraries(eq2_db_updater PRIVATE
  eq2::db
)

eq2_target_defaults(eq2_db_updater)

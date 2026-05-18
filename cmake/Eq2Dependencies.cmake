find_package(Threads REQUIRED)

add_library(eq2_external_threads INTERFACE)
add_library(eq2::external::threads ALIAS eq2_external_threads)
target_link_libraries(eq2_external_threads INTERFACE Threads::Threads)

find_package(ZLIB REQUIRED)

add_library(eq2_external_zlib INTERFACE)
add_library(eq2::external::zlib ALIAS eq2_external_zlib)
target_link_libraries(eq2_external_zlib INTERFACE ZLIB::ZLIB)

option(EQ2_SOURCE2_ENABLE_MARIADB "Enable source2 MariaDB Connector/C support when available" ON)
set(EQ2_MARIADB_ROOT "" CACHE PATH "Optional MariaDB Connector/C SDK root")

add_library(eq2_external_mariadb INTERFACE)
add_library(eq2::external::mariadb ALIAS eq2_external_mariadb)

if(EQ2_SOURCE2_ENABLE_MARIADB)
  find_package(unofficial-libmariadb CONFIG QUIET)

  if(TARGET unofficial::libmariadb)
    target_link_libraries(eq2_external_mariadb INTERFACE unofficial::libmariadb)
    target_compile_definitions(eq2_external_mariadb INTERFACE EQ2_SOURCE2_HAS_MARIADB=1)
    message(STATUS "source2 MariaDB Connector/C support enabled via vcpkg")
  else()
    find_path(EQ2_MARIADB_INCLUDE_DIR
      NAMES mysql.h
      PATHS
        "${EQ2_MARIADB_ROOT}/include"
        "${EQ2_MARIADB_ROOT}/include/mariadb"
        "$ENV{MARIADB_ROOT}/include"
        "$ENV{MARIADB_ROOT}/include/mariadb"
        "$ENV{MYSQL_ROOT}/include"
        "$ENV{MYSQL_ROOT}/include/mysql"
      DOC "MariaDB Connector/C include directory"
    )

    find_library(EQ2_MARIADB_LIBRARY
      NAMES libmariadb mariadb mariadbclient mysqlclient
      PATHS
        "${EQ2_MARIADB_ROOT}/lib"
        "${EQ2_MARIADB_ROOT}/lib/mariadb"
        "$ENV{MARIADB_ROOT}/lib"
        "$ENV{MARIADB_ROOT}/lib/mariadb"
        "$ENV{MYSQL_ROOT}/lib"
      DOC "MariaDB Connector/C library"
    )

    if(EQ2_MARIADB_INCLUDE_DIR AND EQ2_MARIADB_LIBRARY)
      add_library(eq2_external_mariadb_sdk UNKNOWN IMPORTED)
      set_target_properties(eq2_external_mariadb_sdk PROPERTIES
        IMPORTED_LOCATION "${EQ2_MARIADB_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${EQ2_MARIADB_INCLUDE_DIR}"
      )
      target_link_libraries(eq2_external_mariadb INTERFACE eq2_external_mariadb_sdk)
      target_compile_definitions(eq2_external_mariadb INTERFACE EQ2_SOURCE2_HAS_MARIADB=1)
      message(STATUS "source2 MariaDB Connector/C support enabled via SDK: ${EQ2_MARIADB_LIBRARY}")
    else()
      message(STATUS "source2 MariaDB Connector/C support disabled: libmariadb was not found")
    endif()
  endif()
endif()

# Add heavier dependencies here when source2 modules start using them.
# Expected future dependencies include zlib, Boost, OpenSSL,
# Lua, Recast/Detour, readline, glm, and fmt.

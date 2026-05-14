find_package(Threads REQUIRED)

add_library(eq2_external_threads INTERFACE)
add_library(eq2::external::threads ALIAS eq2_external_threads)
target_link_libraries(eq2_external_threads INTERFACE Threads::Threads)

# Add heavier dependencies here when source2 modules start using them.
# Expected future dependencies include MariaDB, zlib, Boost, OpenSSL,
# Lua, Recast/Detour, readline, glm, and fmt.

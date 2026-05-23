cmake_minimum_required(VERSION 3.25)

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 OLD)
  set(CMAKE_POLICY_DEFAULT_CMP0167 OLD)
endif()

project(EQ2Emu
  VERSION 0.1.0
  LANGUAGES CXX
)

option(EQ2EMU_BUILD_LOGIN "Build the EQ2Emu login server" ON)
option(EQ2EMU_BUILD_WORLD "Build the EQ2Emu world server" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Boost REQUIRED COMPONENTS filesystem iostreams program_options regex system thread)
find_package(fmt CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Lua REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(recastnavigation CONFIG REQUIRED)
find_package(unofficial-lua CONFIG REQUIRED)
find_package(unofficial-libmariadb CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

set(EQ2EMU_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/source")
set(EQ2EMU_SERVER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/server")

file(GLOB EQ2EMU_COMMON_SOURCES CONFIGURE_DEPENDS
  "${EQ2EMU_SOURCE_DIR}/common/*.cpp"
  "${EQ2EMU_SOURCE_DIR}/common/Web/*.cpp"
)

if(WIN32)
  list(REMOVE_ITEM EQ2EMU_COMMON_SOURCES "${EQ2EMU_SOURCE_DIR}/common/unix.cpp")
endif()

set(EQ2EMU_COMMON_INCLUDE_DIRS
  "${EQ2EMU_SOURCE_DIR}"
  "${EQ2EMU_SOURCE_DIR}/common"
  "${EQ2EMU_SOURCE_DIR}/LoginServer"
  "${EQ2EMU_SOURCE_DIR}/WorldServer"
  "${EQ2EMU_SOURCE_DIR}/WorldServer/Zone"
  ${LUA_INCLUDE_DIR}
)

set(EQ2EMU_COMMON_LIBS
  Boost::boost
  Boost::filesystem
  Boost::iostreams
  Boost::program_options
  Boost::regex
  Boost::system
  Boost::thread
  fmt::fmt
  glm::glm
  unofficial::lua::lua
  unofficial::lua::lua-cpp
  unofficial::libmariadb
  OpenSSL::SSL
  OpenSSL::Crypto
  ZLIB::ZLIB
)

set(EQ2EMU_RECAST_TARGETS
  RecastNavigation::DebugUtils
  RecastNavigation::Detour
  RecastNavigation::DetourCrowd
  RecastNavigation::DetourTileCache
  RecastNavigation::Recast
)

foreach(recast_target IN LISTS EQ2EMU_RECAST_TARGETS)
  if(TARGET "${recast_target}")
    list(APPEND EQ2EMU_COMMON_LIBS "${recast_target}")
  endif()
endforeach()

function(eq2emu_apply_target_defaults target)
  target_include_directories("${target}" PRIVATE ${EQ2EMU_COMMON_INCLUDE_DIRS})
  target_compile_definitions("${target}" PRIVATE
    EQ2
    INVERSEXY
    $<$<CONFIG:Debug>:DEBUG;_DEBUG>
  )

  if(MSVC)
    target_compile_definitions("${target}" PRIVATE
      WIN32
      _CONSOLE
      _CRT_SECURE_NO_DEPRECATE
      _CRT_SECURE_NO_WARNINGS
      _HAS_STD_BYTE=0
      _SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING
      _WINSOCK_DEPRECATED_NO_WARNINGS
      _WIN32_WINNT=0x0601
      NOGDI
      NOMINMAX
      WIN32_LEAN_AND_MEAN
    )
    target_compile_options("${target}" PRIVATE /W3 /bigobj /Zc:strictStrings-)
  else()
    target_compile_options("${target}" PRIVATE -Wall -Wextra)
  endif()
endfunction()

function(eq2emu_copy_vcpkg_dlls target)
  if(WIN32 AND DEFINED _VCPKG_INSTALLED_DIR)
    add_custom_command(TARGET "${target}" POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_directory
              "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<$<CONFIG:Debug>:debug/>bin"
              "$<TARGET_FILE_DIR:${target}>"
      COMMAND_EXPAND_LISTS
      VERBATIM
    )
  endif()
endfunction()

function(eq2emu_make_server target target_define output_name)
  target_compile_definitions("${target}" PRIVATE "${target_define}")
  eq2emu_apply_target_defaults("${target}")
  target_link_libraries("${target}" PRIVATE ${EQ2EMU_COMMON_LIBS})

  if(WIN32)
    target_link_libraries("${target}" PRIVATE ws2_32)
  endif()

  set_target_properties("${target}" PROPERTIES
    OUTPUT_NAME "${output_name}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )

  eq2emu_copy_vcpkg_dlls("${target}")

  install(TARGETS "${target}" RUNTIME DESTINATION server)
endfunction()

if(EQ2EMU_BUILD_LOGIN)
  file(GLOB EQ2EMU_LOGIN_SOURCES CONFIGURE_DEPENDS
    "${EQ2EMU_SOURCE_DIR}/LoginServer/*.cpp"
    "${EQ2EMU_SOURCE_DIR}/LoginServer/Web/*.cpp"
  )

  add_executable(login ${EQ2EMU_LOGIN_SOURCES} ${EQ2EMU_COMMON_SOURCES})
  eq2emu_make_server(login LOGIN login)
endif()

if(EQ2EMU_BUILD_WORLD)
  file(GLOB_RECURSE EQ2EMU_WORLD_SOURCES CONFIGURE_DEPENDS
    "${EQ2EMU_SOURCE_DIR}/WorldServer/*.cpp"
  )

  add_executable(eq2world ${EQ2EMU_WORLD_SOURCES} ${EQ2EMU_COMMON_SOURCES})
  eq2emu_make_server(eq2world WORLD eq2world)
endif()

install(DIRECTORY "${EQ2EMU_SERVER_DIR}/"
  DESTINATION server
  FILES_MATCHING
    PATTERN "*.xml"
    PATTERN "*.example"
    PATTERN "*.md"
)

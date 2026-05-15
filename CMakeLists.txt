cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED VCPKG_MANIFEST_DIR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/source2/vcpkg.json")
  set(VCPKG_MANIFEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/source2" CACHE PATH
    "vcpkg manifest directory for source2 dependencies"
  )
endif()

project(EQ2Emu
  VERSION 0.1.0
  DESCRIPTION "EQ2Emu source2 migration build"
  LANGUAGES CXX
)

option(EQ2EMU_BUILD_SOURCE2 "Build the source2 migration targets" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(Eq2CompilerOptions)
include(Eq2Dependencies)
include(CTest)

if(EQ2EMU_BUILD_SOURCE2)
  add_subdirectory(source2)
endif()

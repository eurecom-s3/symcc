cmake_minimum_required(VERSION 3.5)
project(SymRuntime)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 \
-Wredundant-decls -Wcast-align \
-Wmissing-include-dirs -Wswitch-default \
-Wextra -Wall -Winvalid-pch -Wredundant-decls -Wformat=2 \
-Wmissing-format-attribute -Wformat-nonliteral")

option(QSYM_BACKEND "Use the Qsym backend instead of our own" OFF)

# Place the final product in the top-level output directory
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# There is list(TRANSFORM ... PREPEND ...), but it's not available before CMake 3.12.
set(SHARED_RUNTIME_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/Config.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/RuntimeCommon.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/LibcWrappers.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Shadow.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/GarbageCollection.cpp)

if (${QSYM_BACKEND})
  add_subdirectory(qsym_backend)
else()
  add_subdirectory(simple_backend)
endif()

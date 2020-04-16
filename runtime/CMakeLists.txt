cmake_minimum_required(VERSION 3.10)
project(SymRuntime)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wredundant-decls -Wcast-align \
-Wmissing-include-dirs -Wswitch-default \
-Wextra -Wall -Winvalid-pch -Wredundant-decls -Wformat=2 \
-Wmissing-format-attribute -Wformat-nonliteral -Werror")

option(QSYM_BACKEND "Use the Qsym backend instead of our own" OFF)

# Place the final product in the top-level output directory
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

if (${QSYM_BACKEND})
  add_subdirectory(qsym_backend)
else()
  add_subdirectory(simple_backend)
endif()

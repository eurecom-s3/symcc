cmake_minimum_required(VERSION 3.14)
project(SymbolicCompiler)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake from ${LLVM_DIR}")

if (${LLVM_PACKAGE_VERSION} VERSION_LESS "8.0" OR
    ${LLVM_PACKAGE_VERSION} VERSION_GREATER_EQUAL "9.0")
  message(WARNING "The software has been developed for LLVM 8; \
it is unlikely to work with other versions!")
endif()

add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wredundant-decls -Wcast-align \
-Wmissing-declarations -Wmissing-include-dirs -Wswitch-enum -Wswitch-default \
-Wextra -Wall -Winvalid-pch -Wredundant-decls -Wformat=2 \
-Wmissing-format-attribute -Wformat-nonliteral")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pedantic -Wall -Wshadow -Wpointer-arith \
-Wcast-qual -Wstrict-prototypes")

add_library(Symbolize MODULE Symbolize.cpp)
add_library(SymRuntime Runtime.c)

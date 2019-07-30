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
-Wmissing-include-dirs -Wswitch-default \
-Wextra -Wall -Winvalid-pch -Wredundant-decls -Wformat=2 \
-Wmissing-format-attribute -Wformat-nonliteral")

# This is the compiler pass that we later load into clang or opt. If LLVM is
# built without RTTI we have to disable it for our library too, otherwise we'll
# get linker errors.
add_library(Symbolize MODULE
  compiler/Symbolize.cpp
  compiler/Pass.cpp
  compiler/Runtime.cpp
  compiler/Main.cpp)
if (NOT LLVM_ENABLE_RTTI)
  set_target_properties(Symbolize PROPERTIES COMPILE_FLAGS "-fno-rtti")
endif()

add_library(SymRuntime SHARED
  runtime/Runtime.cpp
  runtime/LibcWrappers.cpp
  runtime/Shadow.cpp)
target_link_libraries(SymRuntime "z3")

configure_file("compiler/symcc.in" "symcc")

add_subdirectory(test)

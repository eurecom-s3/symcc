# Build the parts of the Qsym backend that are relevant for us

set(QSYM_SOURCE_DIR "qsym/qsym/pintool")

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/expr__gen.cpp
  COMMAND python gen_expr.py ${CMAKE_CURRENT_BINARY_DIR}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${QSYM_SOURCE_DIR}/codegen
  COMMENT "Generating Qsym's expr__gen.cpp"
  VERBATIM
  DEPENDS
  ${QSYM_SOURCE_DIR}/codegen/expr.cpp
  ${QSYM_SOURCE_DIR}/codegen/gen_expr.py)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/expr_builder__gen.cpp
  COMMAND python gen_expr_builder.py ${CMAKE_CURRENT_BINARY_DIR}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${QSYM_SOURCE_DIR}/codegen
  COMMENT "Generating Qsym's expr__gen.cpp"
  VERBATIM
  DEPENDS
  ${QSYM_SOURCE_DIR}/codegen/expr_builder.cpp
  ${QSYM_SOURCE_DIR}/codegen/gen_expr_builder.py)

find_package(LLVM REQUIRED CONFIG)
add_definitions(${LLVM_DEFINITIONS})
include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})

# Qsym doesn't work with old versions of Z3
find_package(Z3 4.5 REQUIRED config)

add_library(SymRuntime SHARED
  ${CMAKE_CURRENT_BINARY_DIR}/expr__gen.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/expr_builder__gen.cpp
  ${QSYM_SOURCE_DIR}/expr_cache.cpp
  ${QSYM_SOURCE_DIR}/expr_evaluate.cpp
  ${QSYM_SOURCE_DIR}/solver.cpp
  ${QSYM_SOURCE_DIR}/dependency.cpp
  ${QSYM_SOURCE_DIR}/logging.cpp
  ${QSYM_SOURCE_DIR}/afl_trace_map.cpp
  ${QSYM_SOURCE_DIR}/allocation.cpp
  ${QSYM_SOURCE_DIR}/call_stack_manager.cpp
  ${QSYM_SOURCE_DIR}/third_party/llvm/range.cpp
  ${QSYM_SOURCE_DIR}/third_party/xxhash/xxhash.cpp
  Runtime.cpp
  ../Config.cpp
  ../RuntimeCommon.cpp
  ../Shadow.cpp
  ../LibcWrappers.cpp)

set_target_properties(SymRuntime PROPERTIES COMPILE_FLAGS
  "-Wno-unused-parameter -Wno-implicit-fallthrough")

target_include_directories(SymRuntime PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}    # for our fake pin.H and Runtime.h
  ${CMAKE_CURRENT_SOURCE_DIR}/.. # for common runtime components
  ${Z3_CXX_INCLUDE_DIRS}         # for Z3 support
  ${QSYM_SOURCE_DIR})

# We need to get the LLVM support component for llvm::APInt.
llvm_map_components_to_libnames(QSYM_LLVM_DEPS support)

target_link_libraries(SymRuntime ${Z3_LIBRARIES} ${QSYM_LLVM_DEPS})

# Build the parts of the Qsym backend that are relevant for us

set(QSYM_SOURCE_DIR "qsym/qsym/pintool")

add_library(SymRuntime SHARED
  ${QSYM_SOURCE_DIR}/expr__gen.cpp
  ${QSYM_SOURCE_DIR}/expr_builder__gen.cpp
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
  ../RuntimeCommon.cpp
  ../Shadow.cpp
  ../LibcWrappers.cpp)

set_target_properties(SymRuntime PROPERTIES COMPILE_FLAGS
  "-Wno-unused-parameter -Wno-implicit-fallthrough")

target_include_directories(SymRuntime PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}    # for our fake pin.H and Runtime.h
  ${CMAKE_CURRENT_SOURCE_DIR}/.. # for common runtime components
  ${QSYM_SOURCE_DIR})

# We need to get the LLVM support component for llvm::APInt.
llvm_map_components_to_libnames(QSYM_LLVM_DEPS support)

target_link_libraries(SymRuntime "z3" ${QSYM_LLVM_DEPS})

# Build the parts of the Qsym backend that are relevant for us

set(QSYM_SOURCE_DIR "qsym/qsym/pintool")

add_library(QsymBackend SHARED
  ${QSYM_SOURCE_DIR}/expr__gen.cpp
  ${QSYM_SOURCE_DIR}/expr_builder__gen.cpp
  ${QSYM_SOURCE_DIR}/expr_cache.cpp
  ${QSYM_SOURCE_DIR}/expr_evaluate.cpp
  ${QSYM_SOURCE_DIR}/solver.cpp
  ${QSYM_SOURCE_DIR}/dependency.cpp
  ${QSYM_SOURCE_DIR}/logging.cpp
  ${QSYM_SOURCE_DIR}/call_stack_manager.cpp
  # ${QSYM_SOURCE_DIR}/afl_trace_map.cpp
  ${QSYM_SOURCE_DIR}/third_party/llvm/range.cpp
  support.cpp)

set_target_properties(QsymBackend PROPERTIES COMPILE_FLAGS
  "-Wno-unused-parameter")

target_include_directories(QsymBackend PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR} # for our fake pin.H
  ${QSYM_SOURCE_DIR})

target_link_libraries(QsymBackend "z3")


if (QSYM_BACKEND)
  set(SYM_TEST_FILECHECK_ARGS "--check-prefix=QSYM --check-prefix=ANY")
else()
  set(SYM_TEST_FILECHECK_ARGS "--check-prefix=SIMPLE --check-prefix=ANY")
endif()

configure_file("lit.site.cfg.in" "lit.site.cfg")

add_custom_target(check
  lit
  --verbose
  --path=${LLVM_TOOLS_BINARY_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Testing the system..."
  USES_TERMINAL)

add_dependencies(check SymRuntime Symbolize)
if (TARGET SymRuntime32)
  add_dependencies(check SymRuntime32)
endif()

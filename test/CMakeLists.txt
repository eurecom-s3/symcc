configure_file("lit.site.cfg.in" "lit.site.cfg")

add_custom_target(test
  lit
  --verbose
  --path=${LLVM_TOOLS_BINARY_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Testing the system..."
  USES_TERMINAL)

add_dependencies(test SymRuntime Symbolize)

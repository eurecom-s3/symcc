configure_file("lit.site.cfg.in" "lit.site.cfg")

add_custom_target(test
  lit
  --verbose
  --path=${LLVM_TOOLS_BINARY_DIR}
  -DSYM_RUNTIME_DIR=$<TARGET_FILE_DIR:SymRuntime>
  -DSYM_PASS=$<TARGET_LINKER_FILE:Symbolize>
  ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Testing the system..."
  USES_TERMINAL)

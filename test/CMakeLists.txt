configure_file("lit.site.cfg.in" "lit.site.cfg")

add_custom_target(test
  lit
  --verbose
  -DSYM_RUNTIME=$<TARGET_LINKER_FILE:SymRuntime>
  -DSYM_PASS=$<TARGET_LINKER_FILE:Symbolize>
  ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Testing the system..."
  USES_TERMINAL)

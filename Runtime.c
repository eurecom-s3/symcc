#include <z3.h>

static Z3_context g_context;
static Z3_solver g_solver;
static Z3_ast g_return_value;
static Z3_ast g_function_arguments[256];

/*
 * Initialization
 */

void _sym_initialize(void) {
  Z3_config cfg;

  cfg = Z3_mk_config();
  Z3_set_param_value(cfg, "model", "true");
  g_context = Z3_mk_context(cfg);
  Z3_del_config(cfg);

  g_solver = Z3_mk_solver(g_context);
  Z3_solver_inc_ref(g_context, g_solver);
}

/*
 * Construction of simple values
 */

Z3_ast _sym_build_integer(uint64_t value, uint8_t bits) {
  return Z3_mk_int(g_context, value, Z3_mk_bv_sort(g_context, bits));
}

/*
 * Arithmetic
 */

Z3_ast _sym_build_add(Z3_ast a, Z3_ast b) {
  return Z3_mk_bvadd(g_context, a, b);
}

Z3_ast _sym_build_signed_rem(Z3_ast a, Z3_ast b) {
  return Z3_mk_bvsrem(g_context, a, b);
}

Z3_ast _sym_build_shift_left(Z3_ast a, Z3_ast b) {
  return Z3_mk_bvshl(g_context, a, b);
}

/*
 * Boolean operations
 */

Z3_ast _sym_build_neg(Z3_ast expr) { return Z3_mk_not(g_context, expr); }

Z3_ast _sym_build_signed_less_than(Z3_ast a, Z3_ast b) {
  return Z3_mk_bvslt(g_context, a, b);
}

Z3_ast _sym_build_equal(Z3_ast a, Z3_ast b) {
  return Z3_mk_eq(g_context, a, b);
}

/*
 * Function-call helpers
 */

void _sym_set_parameter_expression(uint8_t index, Z3_ast expr) {
  g_function_arguments[index] = expr;
}

Z3_ast _sym_get_parameter_expression(uint8_t index) {
  return g_function_arguments[index];
}

void _sym_set_return_expression(Z3_ast expr) { g_return_value = expr; }

Z3_ast _sym_get_return_expression(void) { return g_return_value; }

/*
 * Constraint handling
 */

void _sym_push_path_constraint(Z3_ast constraint) {
  Z3_solver_assert(g_context, g_solver, constraint);
  printf("Solver state:\n%s\n", Z3_solver_to_string(g_context, g_solver));
}

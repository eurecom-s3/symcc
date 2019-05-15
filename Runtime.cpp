#include <cassert>
#include <z3.h>

/* TODO Eventually we'll want to inline as much of this as possible. I'm keeping
   it in C for now because that makes it easier to experiment with new features,
   but I expect that a lot of the functions will stay so simple that we can
   generate the corresponding bitcode directly in the compiler pass. */

#define MAX_FUNCTION_ARGUMENTS 256

static Z3_context g_context;
static Z3_solver g_solver;
static Z3_ast g_return_value;
static Z3_ast g_function_arguments[MAX_FUNCTION_ARGUMENTS];
static Z3_ast g_null_pointer;

extern "C" {

/*
 * Initialization
 */

void _sym_initialize(void) {
  /* TODO prevent repeated initialization */

  Z3_config cfg;

  cfg = Z3_mk_config();
  Z3_set_param_value(cfg, "model", "true");
  g_context = Z3_mk_context(cfg);
  Z3_del_config(cfg);

  g_solver = Z3_mk_solver(g_context);
  Z3_solver_inc_ref(g_context, g_solver);

  g_null_pointer =
      Z3_mk_int(g_context, 0, Z3_mk_bv_sort(g_context, sizeof(void *)));
}

#define SYM_INITIALIZE_ARRAY(bits)                                             \
  void _sym_initialize_array_##bits(Z3_ast expression[], void *value,          \
                                    size_t n_elements) {                       \
    uint##bits##_t *typed_value = static_cast<uint##bits##_t *>(value);        \
    for (size_t i = 0; i < n_elements; i++) {                                  \
      expression[i] = Z3_mk_int(g_context, typed_value[i],                     \
                                Z3_mk_bv_sort(g_context, bits));               \
    }                                                                          \
  }

SYM_INITIALIZE_ARRAY(8)
SYM_INITIALIZE_ARRAY(16)
SYM_INITIALIZE_ARRAY(32)
SYM_INITIALIZE_ARRAY(64)

#undef SYM_INITIALIZE_ARRAY

/*
 * Construction of simple values
 */

Z3_ast _sym_build_integer(uint64_t value, uint8_t bits) {
  return Z3_mk_int(g_context, value, Z3_mk_bv_sort(g_context, bits));
}

uint32_t _sym_build_variable(const char *name, uint32_t value, uint8_t bits) {
  /* TODO find a way to make this more generic, not just for uint32_t */

  /* This function is the connection between the target program and our
     instrumentation; it serves as a way to mark variables as symbolic. We just
     return the concrete value but also set the expression for the return value;
     the instrumentation knows to treat this function specially and check the
     returned expression even though it's an external call. */

  Z3_symbol sym = Z3_mk_string_symbol(g_context, name);
  g_return_value = Z3_mk_const(g_context, sym, Z3_mk_bv_sort(g_context, bits));
  return value;
}

Z3_ast _sym_build_null_pointer(void) { return g_null_pointer; }

/*
 * Arithmetic
 */

Z3_ast _sym_build_add(Z3_ast a, Z3_ast b) {
  return Z3_mk_bvadd(g_context, a, b);
}

Z3_ast _sym_build_mul(Z3_ast a, Z3_ast b) {
  return Z3_mk_bvmul(g_context, a, b);
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

Z3_ast _sym_build_signed_less_equal(Z3_ast a, Z3_ast b) {
  return Z3_mk_bvsle(g_context, a, b);
}

Z3_ast _sym_build_signed_greater_than(Z3_ast a, Z3_ast b) {
  return Z3_mk_bvsgt(g_context, a, b);
}

Z3_ast _sym_build_signed_greater_equal(Z3_ast a, Z3_ast b) {
  return Z3_mk_bvsge(g_context, a, b);
}

Z3_ast _sym_build_unsigned_less_than(Z3_ast a, Z3_ast b) {
  return Z3_mk_bvult(g_context, a, b);
}

Z3_ast _sym_build_unsigned_less_equal(Z3_ast a, Z3_ast b) {
  return Z3_mk_bvule(g_context, a, b);
}

Z3_ast _sym_build_unsigned_greater_than(Z3_ast a, Z3_ast b) {
  return Z3_mk_bvugt(g_context, a, b);
}

Z3_ast _sym_build_unsigned_greater_equal(Z3_ast a, Z3_ast b) {
  return Z3_mk_bvuge(g_context, a, b);
}

Z3_ast _sym_build_equal(Z3_ast a, Z3_ast b) {
  return Z3_mk_eq(g_context, a, b);
}

Z3_ast _sym_build_not_equal(Z3_ast a, Z3_ast b) {
  return Z3_mk_not(g_context, _sym_build_equal(a, b));
}

/*
 * Casts
 */

Z3_ast _sym_build_sext(Z3_ast expr, uint8_t bits) {
  return Z3_mk_sign_ext(g_context, bits, expr);
}

Z3_ast _sym_build_zext(Z3_ast expr, uint8_t bits) {
  return Z3_mk_zero_ext(g_context, bits, expr);
}

Z3_ast _sym_build_trunc(Z3_ast expr, uint8_t bits) {
  return Z3_mk_extract(g_context, bits - 1, 0, expr);
}

/*
 * Function-call helpers
 */

void _sym_set_parameter_expression(uint8_t index, Z3_ast expr) {
  g_function_arguments[index] = expr;
}

void *_sym_get_parameter_expression(uint8_t index) {
  return g_function_arguments[index];
}

void _sym_set_return_expression(Z3_ast expr) { g_return_value = expr; }

Z3_ast _sym_get_return_expression(void) { return g_return_value; }

/*
 * Constraint handling
 */

Z3_ast _sym_push_path_constraint(Z3_ast constraint, int taken) {
  constraint = Z3_simplify(g_context, constraint);

  /* Check the easy cases first: if simplification reduced the constraint to
     "true" or "false", there is no point in trying to solve the negation or *
     pushing the constraint to the solver... */

  if (Z3_is_eq_ast(g_context, constraint, Z3_mk_true(g_context))) {
    assert(taken && "We have taken an impossible branch");
    return constraint;
  }

  if (Z3_is_eq_ast(g_context, constraint, Z3_mk_false(g_context))) {
    assert(!taken && "We have taken an impossible branch");
    return Z3_mk_not(g_context, constraint);
  }

  /* Generate a solution for the alternative */
  Z3_ast not_constraint =
      Z3_simplify(g_context, Z3_mk_not(g_context, constraint));

  Z3_solver_push(g_context, g_solver);
  Z3_solver_assert(g_context, g_solver, taken ? not_constraint : constraint);
  printf("Trying to solve:\n%s\n", Z3_solver_to_string(g_context, g_solver));

  Z3_lbool feasible = Z3_solver_check(g_context, g_solver);
  if (feasible == Z3_L_TRUE) {
    Z3_model model = Z3_solver_get_model(g_context, g_solver);
    Z3_model_inc_ref(g_context, model);
    printf("Found diverging input:\n%s\n",
           Z3_model_to_string(g_context, model));
    Z3_model_dec_ref(g_context, model);
  } else {
    printf("Can't find a diverging input at this point\n");
  }

  Z3_solver_pop(g_context, g_solver, 1);

  /* Assert the actual path constraint */
  Z3_ast newConstraint = (taken ? constraint : not_constraint);
  Z3_solver_assert(g_context, g_solver, newConstraint);
  assert((Z3_solver_check(g_context, g_solver) == Z3_L_TRUE) &&
         "Asserting infeasible path constraint");
  return newConstraint;
}
}

#include "Runtime.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <set>

#ifdef DEBUG_RUNTIME
#include <iostream>
// Helper to print pointers properly.
#define P(ptr) static_cast<void *>(ptr)
#endif

#define FSORT(is_double)                                                       \
  (is_double ? Z3_mk_fpa_sort_double(g_context)                                \
             : Z3_mk_fpa_sort_single(g_context))

/* TODO Eventually we'll want to inline as much of this as possible. I'm keeping
   it in C for now because that makes it easier to experiment with new features,
   but I expect that a lot of the functions will stay so simple that we can
   generate the corresponding bitcode directly in the compiler pass. */

namespace {

constexpr int kMaxFunctionArguments = 256;

/// Memory regions represent a consecutive range of allocated bytes in memory.
/// We assume that there can only ever be a single allocation per address, so
/// the regions do not overlap.
struct MemoryRegion {
  uint8_t *start, *end; // end is one past the last byte
  Z3_ast *shadow;

  bool operator<(const MemoryRegion &other) const { return end <= other.start; }
};

bool operator<(const MemoryRegion &r, uint8_t *addr) { return r.end <= addr; }
bool operator<(uint8_t *addr, const MemoryRegion &r) { return addr < r.start; }

#ifdef DEBUG_RUNTIME
std::ostream &operator<<(std::ostream &out, const MemoryRegion &region) {
  out << "<" << P(region.start) << ", " << P(region.end) << ">";
  return out;
}
#endif

/// Indicate whether the runtime has been initialized.
bool g_initialized = false;

/// The global Z3 context.
Z3_context g_context;

/// The global floating-point rounding mode.
Z3_ast g_rounding_mode;

/// The global Z3 solver.
Z3_solver g_solver; // TODO make thread-local

/// Global storage for function parameters and the return value.
Z3_ast g_return_value;
Z3_ast g_function_arguments[kMaxFunctionArguments];
// TODO make thread-local

// Some global constants for efficiency.
Z3_ast g_null_pointer, g_true, g_false;

/// The set of known memory regions. The container is a sorted set to make
/// retrieval by address efficient. Remember that we assume regions to be
/// non-overlapping.
std::set<MemoryRegion, std::less<>> g_memory_regions;

#ifdef DEBUG_RUNTIME
/// Make sure that g_memory_regions doesn't contain any overlapping memory
/// regions.
void assert_memory_region_invariant() {
  uint8_t *last_end = nullptr;
  for (auto &region : g_memory_regions) {
    assert((region.start >= last_end) && "Overlapping memory regions");
    last_end = region.end;
  }
}
#else
#define assert_memory_region_invariant() ((void)0)
#endif

#ifdef DEBUG_RUNTIME
void dump_known_regions() {
  std::cout << "Known regions:" << std::endl;
  for (auto &region : g_memory_regions) {
    std::cout << "  " << region << std::endl;
  }
}
#endif

} // namespace

void initialize_interception();

void _sym_initialize(void) {
  if (g_initialized)
    return;

  g_initialized = true;

#ifdef DEBUG_RUNTIME
  std::cout << "Initializing symbolic runtime" << std::endl;
#endif

  Z3_config cfg;

  cfg = Z3_mk_config();
  Z3_set_param_value(cfg, "model", "true");
  g_context = Z3_mk_context(cfg);
  Z3_del_config(cfg);

  g_rounding_mode = Z3_mk_fpa_round_nearest_ties_to_even(g_context);

  g_solver = Z3_mk_solver(g_context);
  Z3_solver_inc_ref(g_context, g_solver);

  g_null_pointer =
      Z3_mk_int(g_context, 0, Z3_mk_bv_sort(g_context, 8 * sizeof(void *)));
  g_true = Z3_mk_true(g_context);
  g_false = Z3_mk_false(g_context);

  initialize_interception();
}

#define SYM_INITIALIZE_ARRAY(bits)                                             \
  extern "C" void _sym_initialize_array_##bits(                                \
      Z3_ast expression[], void *value, size_t n_elements) {                   \
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

Z3_ast _sym_build_integer(uint64_t value, uint8_t bits) {
  return Z3_mk_int(g_context, value, Z3_mk_bv_sort(g_context, bits));
}

Z3_ast _sym_build_float(double value, int is_double) {
  return Z3_mk_fpa_numeral_double(g_context, value,
                                  is_double ? Z3_mk_fpa_sort_double(g_context)
                                            : Z3_mk_fpa_sort_single(g_context));
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
Z3_ast _sym_build_true(void) { return g_true; }
Z3_ast _sym_build_false(void) { return g_false; }

Z3_ast _sym_build_add(Z3_ast a, Z3_ast b) {
  return Z3_mk_bvadd(g_context, a, b);
}

Z3_ast _sym_build_sub(Z3_ast a, Z3_ast b) {
  return Z3_mk_bvsub(g_context, a, b);
}

Z3_ast _sym_build_mul(Z3_ast a, Z3_ast b) {
  return Z3_mk_bvmul(g_context, a, b);
}

Z3_ast _sym_build_unsigned_div(Z3_ast a, Z3_ast b) {
  return Z3_mk_bvudiv(g_context, a, b);
}

Z3_ast _sym_build_signed_div(Z3_ast a, Z3_ast b) {
  return Z3_mk_bvsdiv(g_context, a, b);
}

Z3_ast _sym_build_unsigned_rem(Z3_ast a, Z3_ast b) {
  return Z3_mk_bvurem(g_context, a, b);
}

Z3_ast _sym_build_signed_rem(Z3_ast a, Z3_ast b) {
  return Z3_mk_bvsrem(g_context, a, b);
}

Z3_ast _sym_build_shift_left(Z3_ast a, Z3_ast b) {
  return Z3_mk_bvshl(g_context, a, b);
}

Z3_ast _sym_build_logical_shift_right(Z3_ast a, Z3_ast b) {
  return Z3_mk_bvlshr(g_context, a, b);
}

Z3_ast _sym_build_arithmetic_shift_right(Z3_ast a, Z3_ast b) {
  return Z3_mk_bvashr(g_context, a, b);
}

Z3_ast _sym_build_fp_add(Z3_ast a, Z3_ast b) {
  return Z3_mk_fpa_add(g_context, g_rounding_mode, a, b);
}

Z3_ast _sym_build_fp_sub(Z3_ast a, Z3_ast b) {
  return Z3_mk_fpa_sub(g_context, g_rounding_mode, a, b);
}

Z3_ast _sym_build_fp_mul(Z3_ast a, Z3_ast b) {
  return Z3_mk_fpa_mul(g_context, g_rounding_mode, a, b);
}

Z3_ast _sym_build_fp_div(Z3_ast a, Z3_ast b) {
  return Z3_mk_fpa_div(g_context, g_rounding_mode, a, b);
}

Z3_ast _sym_build_fp_rem(Z3_ast a, Z3_ast b) {
  return Z3_mk_fpa_rem(g_context, a, b);
}

Z3_ast _sym_build_fp_abs(Z3_ast a) { return Z3_mk_fpa_abs(g_context, a); }

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

Z3_ast _sym_build_and(Z3_ast a, Z3_ast b) {
  Z3_ast args[] = {a, b};
  return Z3_mk_and(g_context, 2, args);
}

Z3_ast _sym_build_or(Z3_ast a, Z3_ast b) {
  Z3_ast args[] = {a, b};
  return Z3_mk_or(g_context, 2, args);
}

Z3_ast _sym_build_xor(Z3_ast a, Z3_ast b) { return Z3_mk_xor(g_context, a, b); }

Z3_ast _sym_build_float_ordered_greater_than(Z3_ast a, Z3_ast b) {
  return Z3_mk_fpa_gt(g_context, a, b);
}

Z3_ast _sym_build_float_ordered_greater_equal(Z3_ast a, Z3_ast b) {
  return Z3_mk_fpa_geq(g_context, a, b);
}

Z3_ast _sym_build_float_ordered_less_than(Z3_ast a, Z3_ast b) {
  return Z3_mk_fpa_lt(g_context, a, b);
}

Z3_ast _sym_build_float_ordered_less_equal(Z3_ast a, Z3_ast b) {
  return Z3_mk_fpa_leq(g_context, a, b);
}

Z3_ast _sym_build_float_ordered_equal(Z3_ast a, Z3_ast b) {
  return Z3_mk_fpa_eq(g_context, a, b);
}

Z3_ast _sym_build_float_ordered_not_equal(Z3_ast a, Z3_ast b) {
  return Z3_mk_not(g_context, _sym_build_float_ordered_equal(a, b));
}

Z3_ast _sym_build_float_unordered(Z3_ast a, Z3_ast b) {
  Z3_ast checks[2];
  checks[0] = Z3_mk_fpa_is_nan(g_context, a);
  checks[1] = Z3_mk_fpa_is_nan(g_context, b);
  return Z3_mk_or(g_context, 2, checks);
}

Z3_ast _sym_build_float_unordered_greater_than(Z3_ast a, Z3_ast b) {
  Z3_ast checks[3];
  checks[0] = Z3_mk_fpa_is_nan(g_context, a);
  checks[1] = Z3_mk_fpa_is_nan(g_context, b);
  checks[2] = _sym_build_float_ordered_greater_than(a, b);
  return Z3_mk_or(g_context, 2, checks);
}

Z3_ast _sym_build_float_unordered_greater_equal(Z3_ast a, Z3_ast b) {
  Z3_ast checks[3];
  checks[0] = Z3_mk_fpa_is_nan(g_context, a);
  checks[1] = Z3_mk_fpa_is_nan(g_context, b);
  checks[2] = _sym_build_float_ordered_greater_equal(a, b);
  return Z3_mk_or(g_context, 2, checks);
}

Z3_ast _sym_build_float_unordered_less_than(Z3_ast a, Z3_ast b) {
  Z3_ast checks[3];
  checks[0] = Z3_mk_fpa_is_nan(g_context, a);
  checks[1] = Z3_mk_fpa_is_nan(g_context, b);
  checks[2] = _sym_build_float_ordered_less_than(a, b);
  return Z3_mk_or(g_context, 2, checks);
}

Z3_ast _sym_build_float_unordered_less_equal(Z3_ast a, Z3_ast b) {
  Z3_ast checks[3];
  checks[0] = Z3_mk_fpa_is_nan(g_context, a);
  checks[1] = Z3_mk_fpa_is_nan(g_context, b);
  checks[2] = _sym_build_float_ordered_less_equal(a, b);
  return Z3_mk_or(g_context, 2, checks);
}

Z3_ast _sym_build_float_unordered_equal(Z3_ast a, Z3_ast b) {
  Z3_ast checks[3];
  checks[0] = Z3_mk_fpa_is_nan(g_context, a);
  checks[1] = Z3_mk_fpa_is_nan(g_context, b);
  checks[2] = _sym_build_float_ordered_equal(a, b);
  return Z3_mk_or(g_context, 2, checks);
}

Z3_ast _sym_build_float_unordered_not_equal(Z3_ast a, Z3_ast b) {
  Z3_ast checks[3];
  checks[0] = Z3_mk_fpa_is_nan(g_context, a);
  checks[1] = Z3_mk_fpa_is_nan(g_context, b);
  checks[2] = _sym_build_float_ordered_not_equal(a, b);
  return Z3_mk_or(g_context, 2, checks);
}

Z3_ast _sym_build_sext(Z3_ast expr, uint8_t bits) {
  return Z3_mk_sign_ext(g_context, bits, expr);
}

Z3_ast _sym_build_zext(Z3_ast expr, uint8_t bits) {
  return Z3_mk_zero_ext(g_context, bits, expr);
}

Z3_ast _sym_build_trunc(Z3_ast expr, uint8_t bits) {
  return Z3_mk_extract(g_context, bits - 1, 0, expr);
}

Z3_ast _sym_build_int_to_float(Z3_ast value, int is_double, int is_signed) {
  return is_signed ? Z3_mk_fpa_to_fp_signed(g_context, g_rounding_mode, value,
                                            FSORT(is_double))
                   : Z3_mk_fpa_to_fp_unsigned(g_context, g_rounding_mode, value,
                                              FSORT(is_double));
}

Z3_ast _sym_build_float_to_float(Z3_ast expr, int to_double) {
  return Z3_mk_fpa_to_fp_float(g_context, g_rounding_mode, expr,
                               FSORT(to_double));
}

Z3_ast _sym_build_bits_to_float(Z3_ast expr, int to_double) {
  return Z3_mk_fpa_to_fp_bv(g_context, expr, FSORT(to_double));
}

Z3_ast _sym_build_float_to_bits(Z3_ast expr) {
  return Z3_mk_fpa_to_ieee_bv(g_context, expr);
}

Z3_ast _sym_build_float_to_signed_integer(Z3_ast expr, uint8_t bits) {
  return Z3_mk_fpa_to_sbv(g_context, Z3_mk_fpa_round_toward_zero(g_context),
                          expr, bits);
}

Z3_ast _sym_build_float_to_unsigned_integer(Z3_ast expr, uint8_t bits) {
  return Z3_mk_fpa_to_ubv(g_context, Z3_mk_fpa_round_toward_zero(g_context),
                          expr, bits);
}

void _sym_set_parameter_expression(uint8_t index, Z3_ast expr) {
  g_function_arguments[index] = expr;
}

void *_sym_get_parameter_expression(uint8_t index) {
  return g_function_arguments[index];
}

void _sym_set_return_expression(Z3_ast expr) { g_return_value = expr; }

Z3_ast _sym_get_return_expression(void) { return g_return_value; }

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

void _sym_register_memory(uint8_t *addr, Z3_ast *shadow, size_t length) {
  assert_memory_region_invariant();

#ifdef DEBUG_RUNTIME
  std::cout << "Registering memory from " << P(addr) << " to "
            << P(addr + length) << std::endl;
#endif

  // Remove overlapping regions, if any.
  auto first = g_memory_regions.lower_bound(addr);
  auto last = g_memory_regions.upper_bound(addr + length - 1);
#ifdef DEBUG_RUNTIME
  if (first != last)
    printf("Erasing %ld memory objects\n", std::distance(first, last));
#endif
  g_memory_regions.erase(first, last);

  g_memory_regions.insert({addr, addr + length, shadow});
}

void _sym_initialize_memory(uint8_t *addr, Z3_ast *shadow, size_t length) {
#ifdef DEBUG_RUNTIME
  std::cout << "Initializing " << length << " bytes of memory at " << P(addr)
            << std::endl;
#endif

  for (size_t i = 0; i < length; i++) {
    shadow[i] = _sym_build_integer(addr[i], 8);
  }

  _sym_register_memory(addr, shadow, length);
}

Z3_ast _sym_read_memory(uint8_t *addr, size_t length, bool little_endian) {
  assert_memory_region_invariant();
  assert(length && "Invalid query for zero-length memory region");

#ifdef DEBUG_RUNTIME
  std::cout << "Reading " << length << " bytes from address " << P(addr)
            << std::endl;
  dump_known_regions();
#endif

  auto region = g_memory_regions.find(addr);

#ifdef DEBUG_RUNTIME
  if (region != g_memory_regions.end())
    std::cout << "Found region " << *region << std::endl;
#endif

  assert((region != g_memory_regions.end()) && (region->start <= addr) &&
         (addr + length <= region->end) && "Unknown memory region");

  Z3_ast *shadow = &region->shadow[addr - region->start];
  Z3_ast expr = shadow[0];
  for (size_t i = 1; i < length; i++) {
    // TODO For uninitialized memory, create a constant expression holding the
    // actual memory contents.
    expr = little_endian ? Z3_mk_concat(g_context, shadow[i], expr)
                         : Z3_mk_concat(g_context, expr, shadow[i]);
  }

  return expr;
}

void _sym_write_memory(uint8_t *addr, size_t length, Z3_ast expr,
                       bool little_endian) {
  assert_memory_region_invariant();
  assert(length && "Invalid query for zero-length memory region");

#ifdef DEBUG_RUNTIME
  std::cout << "Writing " << length << " bytes to address " << P(addr)
            << std::endl;
  dump_known_regions();
#endif

  auto region = g_memory_regions.find(addr);

#ifdef DEBUG_RUNTIME
  if (region != g_memory_regions.end())
    std::cout << "Found region " << *region << std::endl;
#endif

  assert((region != g_memory_regions.end()) && (region->start <= addr) &&
         (addr + length <= region->end) && "Unknown memory region");

  Z3_ast *shadow = &region->shadow[addr - region->start];
  for (size_t i = 0; i < length; i++) {
    shadow[i] = little_endian
                    ? Z3_mk_extract(g_context, 8 * (i + 1) - 1, 8 * i, expr)
                    : Z3_mk_extract(g_context, (length - i) * 8 - 1,
                                    (length - i - 1) * 8, expr);
  }
}

void _sym_memcpy(uint8_t *dest, uint8_t *src, size_t length) {
  assert_memory_region_invariant();

  auto srcRegion = g_memory_regions.find(src);
  assert((srcRegion != g_memory_regions.end()) &&
         (src + length <= srcRegion->end) && "Unknown memory region");
  Z3_ast *srcShadow = &srcRegion->shadow[src - srcRegion->start];

  auto destRegion = g_memory_regions.find(dest);
  assert((destRegion != g_memory_regions.end()) &&
         (dest + length <= destRegion->end) && "Unknown memory region");
  Z3_ast *destShadow = &destRegion->shadow[dest - destRegion->start];

  memcpy(destShadow, srcShadow, length * sizeof(Z3_ast));
}

void _sym_memset(uint8_t *memory, Z3_ast value, size_t length) {
  assert_memory_region_invariant();

  auto region = g_memory_regions.find(memory);
  assert((region != g_memory_regions.end()) &&
         (memory + length <= region->end) && "Unknown memory region");

  Z3_ast *shadow = &region->shadow[memory - region->start];
  for (size_t index = 0; index < length; index++) {
    shadow[index] = value;
  }
}

Z3_ast _sym_build_extract(Z3_ast expr, uint64_t offset, uint64_t length,
                          bool little_endian) {
  unsigned totalBits =
      Z3_get_bv_sort_size(g_context, Z3_get_sort(g_context, expr));
  assert((totalBits % 8 == 0) && "Aggregate type contains partial bytes");

  Z3_ast result;
  if (little_endian) {
    result = Z3_mk_extract(g_context, totalBits - offset * 8 - 1,
                           totalBits - offset * 8 - 8, expr);
    for (size_t i = 1; i < length; i++) {
      result = Z3_mk_concat(
          g_context, result,
          Z3_mk_extract(g_context, totalBits - (offset + i) * 8 - 1,
                        totalBits - (offset + i + 1) * 8, expr));
    }
  } else {
    result = Z3_mk_extract(g_context, totalBits - offset * 8 - 1,
                           totalBits - (offset + length) * 8, expr);
  }

  return result;
}

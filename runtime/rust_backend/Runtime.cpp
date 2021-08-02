// This file is part of SymCC.
//
// SymCC is free software: you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// SymCC is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with
// SymCC. If not, see <https://www.gnu.org/licenses/>.

#include <Runtime.h>
#include <RustRuntime.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstring>
#include <iostream>
#include <set>
#include <vector>

#ifndef NDEBUG
#include <chrono>
#endif

#include "Config.h"
#include "GarbageCollection.h"
#include "LibcWrappers.h"
#include "Shadow.h"

#ifndef NDEBUG
// Helper to print pointers properly.
#define P(ptr) reinterpret_cast<void *>(ptr)
#endif

/* TODO Eventually we'll want to inline as much of this as possible. I'm keeping
   it in C for now because that makes it easier to experiment with new features,
   but I expect that a lot of the functions will stay so simple that we can
   generate the corresponding bitcode directly in the compiler pass. */

namespace {

/// Indicate whether the runtime has been initialized.
std::atomic_flag g_initialized = ATOMIC_FLAG_INIT;

FILE *g_log = stderr;

#ifndef NDEBUG
[[maybe_unused]] void dump_known_regions() {
  std::cerr << "Known regions:" << std::endl;
  for (const auto &[page, shadow] : g_shadow_pages) {
    std::cerr << "  " << P(page) << " shadowed by " << P(shadow) << std::endl;
  }
}

#endif

/// The set of all expressions we have ever passed to client code.
std::set<SymExpr> allocatedExpressions;

SymExpr registerExpression(SymExpr expr) {
  allocatedExpressions.insert(expr);
  return expr;
}

// To understand why the following functions exist, read the Bits Helper section
// in the README.

// Get the bit width out of a SymExpr.
uint8_t symexpr_width(SymExpr expr) {
  return (uint8_t)((uintptr_t)expr & UINT8_MAX);
}

// Get the id out of a SymExpr (which is an RSymExpr).
RSymExpr symexpr_id(SymExpr expr) { return (uintptr_t)expr >> 8; }

// Construct a SymExpr from a RSymExpr and a bit width.
SymExpr symexpr(RSymExpr expr, uint8_t width) {
  if (expr == 0) {
    // ensure that 0 RSymExpr still maps to 0 in SymExpr, as this is a special
    // value for the rest of the backend.
    return 0;
  }
  // ensure that the RSymExpr fits inside the SymExpr.
  assert((((expr << 8) >> 8) == expr) && "expr is too large to be stored");
  return (SymExpr)((expr << 8) | width);
}
} // namespace

void _sym_initialize(void) {
  if (g_initialized.test_and_set())
    return;

#ifndef NDEBUG
  std::cerr << "Initializing symbolic runtime" << std::endl;
#endif

  loadConfig();
  initLibcWrappers();
  std::cerr << "This is SymCC running with the Rust backend" << std::endl;

  if (g_config.logFile.empty()) {
    g_log = stderr;
  } else {
    g_log = fopen(g_config.logFile.c_str(), "w");
  }
}

SymExpr _sym_build_integer(uint64_t value, uint8_t bits) {
  return registerExpression(symexpr(_rsym_build_integer(value, bits), bits));
}

SymExpr _sym_build_integer128(uint64_t high, uint64_t low) {
  return registerExpression(symexpr(_rsym_build_integer128(high, low), 128));
}

SymExpr _sym_build_float(double value, int is_double) {
  return registerExpression(
      symexpr(_rsym_build_float(value, is_double), is_double ? 64 : 32));
}

SymExpr _sym_get_input_byte(size_t offset) {
  return registerExpression(symexpr(_rsym_get_input_byte(offset), 8));
}

SymExpr _sym_build_null_pointer(void) {
  return registerExpression(
      symexpr(_rsym_build_null_pointer(), sizeof(uintptr_t) * 8));
}

SymExpr _sym_build_true(void) {
  return registerExpression(symexpr(_rsym_build_true(), 0));
}

SymExpr _sym_build_false(void) {
  return registerExpression(symexpr(_rsym_build_false(), 0));
}

SymExpr _sym_build_bool(bool value) {
  return registerExpression(symexpr(_rsym_build_bool(value), 0));
}

#define DEF_UNARY_EXPR_BUILDER(name)                                           \
  SymExpr _sym_build_##name(SymExpr expr) {                                    \
    return registerExpression(                                                 \
        symexpr(_rsym_build_##name(symexpr_id(expr)), symexpr_width(expr)));   \
  }

DEF_UNARY_EXPR_BUILDER(neg)

#define DEF_BINARY_BV_EXPR_BUILDER(name)                                       \
  SymExpr _sym_build_##name(SymExpr a, SymExpr b) {                            \
    return registerExpression(symexpr(                                         \
        _rsym_build_##name(symexpr_id(a), symexpr_id(b)), symexpr_width(a)));  \
  }

DEF_BINARY_BV_EXPR_BUILDER(add)
DEF_BINARY_BV_EXPR_BUILDER(sub)
DEF_BINARY_BV_EXPR_BUILDER(mul)
DEF_BINARY_BV_EXPR_BUILDER(unsigned_div)
DEF_BINARY_BV_EXPR_BUILDER(signed_div)
DEF_BINARY_BV_EXPR_BUILDER(unsigned_rem)
DEF_BINARY_BV_EXPR_BUILDER(signed_rem)
DEF_BINARY_BV_EXPR_BUILDER(shift_left)
DEF_BINARY_BV_EXPR_BUILDER(logical_shift_right)
DEF_BINARY_BV_EXPR_BUILDER(arithmetic_shift_right)

#define DEF_BINARY_BOOL_EXPR_BUILDER(name)                                     \
  SymExpr _sym_build_##name(SymExpr a, SymExpr b) {                            \
    return registerExpression(                                                 \
        symexpr(_rsym_build_##name(symexpr_id(a), symexpr_id(b)), 0));         \
  }

DEF_BINARY_BOOL_EXPR_BUILDER(signed_less_than)
DEF_BINARY_BOOL_EXPR_BUILDER(signed_less_equal)
DEF_BINARY_BOOL_EXPR_BUILDER(signed_greater_than)
DEF_BINARY_BOOL_EXPR_BUILDER(signed_greater_equal)
DEF_BINARY_BOOL_EXPR_BUILDER(unsigned_less_than)
DEF_BINARY_BOOL_EXPR_BUILDER(unsigned_less_equal)
DEF_BINARY_BOOL_EXPR_BUILDER(unsigned_greater_than)
DEF_BINARY_BOOL_EXPR_BUILDER(unsigned_greater_equal)
DEF_BINARY_BOOL_EXPR_BUILDER(equal)

DEF_BINARY_BV_EXPR_BUILDER(and)
DEF_BINARY_BV_EXPR_BUILDER(or)
DEF_BINARY_BV_EXPR_BUILDER(bool_xor)
DEF_BINARY_BV_EXPR_BUILDER(xor)

DEF_BINARY_BOOL_EXPR_BUILDER(float_ordered_greater_than)
DEF_BINARY_BOOL_EXPR_BUILDER(float_ordered_greater_equal)
DEF_BINARY_BOOL_EXPR_BUILDER(float_ordered_less_than)
DEF_BINARY_BOOL_EXPR_BUILDER(float_ordered_less_equal)
DEF_BINARY_BOOL_EXPR_BUILDER(float_ordered_equal)

DEF_BINARY_BV_EXPR_BUILDER(fp_add)
DEF_BINARY_BV_EXPR_BUILDER(fp_sub)
DEF_BINARY_BV_EXPR_BUILDER(fp_mul)
DEF_BINARY_BV_EXPR_BUILDER(fp_div)
DEF_BINARY_BV_EXPR_BUILDER(fp_rem)

#undef DEF_BINARY_BV_EXPR_BUILDER

DEF_UNARY_EXPR_BUILDER(fp_abs)

DEF_UNARY_EXPR_BUILDER(not )
DEF_BINARY_BOOL_EXPR_BUILDER(not_equal)

#undef DEF_UNARY_EXPR_BUILDER

DEF_BINARY_BOOL_EXPR_BUILDER(bool_and)
DEF_BINARY_BOOL_EXPR_BUILDER(bool_or)

DEF_BINARY_BOOL_EXPR_BUILDER(float_ordered_not_equal)
DEF_BINARY_BOOL_EXPR_BUILDER(float_ordered)
DEF_BINARY_BOOL_EXPR_BUILDER(float_unordered)

DEF_BINARY_BOOL_EXPR_BUILDER(float_unordered_greater_than)
DEF_BINARY_BOOL_EXPR_BUILDER(float_unordered_greater_equal)
DEF_BINARY_BOOL_EXPR_BUILDER(float_unordered_less_than)
DEF_BINARY_BOOL_EXPR_BUILDER(float_unordered_less_equal)
DEF_BINARY_BOOL_EXPR_BUILDER(float_unordered_equal)
DEF_BINARY_BOOL_EXPR_BUILDER(float_unordered_not_equal)

#undef DEF_BINARY_BOOL_EXPR_BUILDER

SymExpr _sym_build_sext(SymExpr expr, uint8_t bits) {
  return registerExpression(symexpr(_rsym_build_sext(symexpr_id(expr), bits),
                                    symexpr_width(expr) + bits));
}

SymExpr _sym_build_zext(SymExpr expr, uint8_t bits) {
  return registerExpression(symexpr(_rsym_build_zext(symexpr_id(expr), bits),
                                    symexpr_width(expr) + bits));
}

SymExpr _sym_build_trunc(SymExpr expr, uint8_t bits) {
  return registerExpression(
      symexpr(_rsym_build_trunc(symexpr_id(expr), bits), bits));
}

SymExpr _sym_build_int_to_float(SymExpr expr, int is_double, int is_signed) {
  return registerExpression(
      symexpr(_rsym_build_int_to_float(symexpr_id(expr), is_double, is_signed),
              is_double ? 64 : 32));
}

SymExpr _sym_build_float_to_float(SymExpr expr, int to_double) {
  return registerExpression(
      symexpr(_rsym_build_float_to_float(symexpr_id(expr), to_double),
              to_double ? 64 : 32));
}

SymExpr _sym_build_bits_to_float(SymExpr expr, int to_double) {
  if (expr == 0)
    return 0;

  return registerExpression(
      symexpr(_rsym_build_bits_to_float(symexpr_id(expr), to_double),
              to_double ? 64 : 32));
}

SymExpr _sym_build_float_to_bits(SymExpr expr) {
  if (expr == nullptr)
    return nullptr;
  return registerExpression(symexpr(_rsym_build_float_to_bits(symexpr_id(expr)),
                                    symexpr_width(expr)));
}

SymExpr _sym_build_float_to_signed_integer(SymExpr expr, uint8_t bits) {
  return registerExpression(symexpr(
      _rsym_build_float_to_signed_integer(symexpr_id(expr), bits), bits));
}

SymExpr _sym_build_float_to_unsigned_integer(SymExpr expr, uint8_t bits) {
  return registerExpression(symexpr(
      _rsym_build_float_to_unsigned_integer(symexpr_id(expr), bits), bits));
}

SymExpr _sym_build_bool_to_bits(SymExpr expr, uint8_t bits) {
  return registerExpression(
      symexpr(_rsym_build_bool_to_bits(symexpr_id(expr), bits), bits));
}

void _sym_push_path_constraint(SymExpr constraint, int taken,
                               uintptr_t site_id) {
  if (constraint == 0)
    return;
  _rsym_push_path_constraint(symexpr_id(constraint), taken, site_id);
}

SymExpr _sym_concat_helper(SymExpr a, SymExpr b) {
  return registerExpression(
      symexpr(_rsym_concat_helper(symexpr_id(a), symexpr_id(b)),
              symexpr_width(a) + symexpr_width(b)));
}

SymExpr _sym_extract_helper(SymExpr expr, size_t first_bit, size_t last_bit) {
  return registerExpression(
      symexpr(_rsym_extract_helper(symexpr_id(expr), first_bit, last_bit),
              first_bit - last_bit + 1));
}

size_t _sym_bits_helper(SymExpr expr) { return symexpr_width(expr); }

void _sym_notify_call(uintptr_t loc) { _rsym_notify_call(loc); }
void _sym_notify_ret(uintptr_t loc) { _rsym_notify_ret(loc); }
void _sym_notify_basic_block(uintptr_t loc) { _rsym_notify_basic_block(loc); }

/* Debugging */
const char *_sym_expr_to_string(SymExpr) { return nullptr; }

bool _sym_feasible(SymExpr) { return false; }

/* Garbage collection */
void _sym_collect_garbage() {
  if (allocatedExpressions.size() < g_config.garbageCollectionThreshold)
    return;

#ifndef NDEBUG
  auto start = std::chrono::high_resolution_clock::now();
  auto startSize = allocatedExpressions.size();
#endif

  std::vector<RSymExpr> unreachable_expressions;

  auto reachableExpressions = collectReachableExpressions();
  for (auto expr_it = allocatedExpressions.begin();
       expr_it != allocatedExpressions.end();) {
    if (reachableExpressions.count(*expr_it) == 0) {
      unreachable_expressions.push_back(symexpr_id(*expr_it));
      expr_it = allocatedExpressions.erase(expr_it);
    } else {
      ++expr_it;
    }
  }
  if (unreachable_expressions.size() > 0) {
    _rsym_expression_unreachable(unreachable_expressions.data(),
                                 unreachable_expressions.size());
  }

#ifndef NDEBUG
  auto end = std::chrono::high_resolution_clock::now();
  auto endSize = allocatedExpressions.size();

  std::cerr << "After garbage collection: " << endSize
            << " expressions remain (before: " << startSize << ")" << std::endl
            << "\t(collection took "
            << std::chrono::duration_cast<std::chrono::milliseconds>(end -
                                                                     start)
                   .count()
            << " milliseconds)" << std::endl;
#endif
}

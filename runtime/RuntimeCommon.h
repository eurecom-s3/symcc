// Run-time library interface                                  -*- C++ -*-
//
// This header defines the interface of the run-time library. It is not actually
// used anywhere because the compiler pass inserts calls to the library
// functions at the level of LLVM bitcode, but it serves as documentation of the
// intended interface. Unless documented otherwise, functions taking symbolic
// expressions can't handle null values (i.e., they shouldn't be called for
// concrete values); exceptions are made if it's too difficult to check for
// concreteness in bitcode.
//
// Whoever uses this file has to define the type "SymExpr" first; we use it to
// keep this header independent of the back-end implementation.

// This file is part of the SymCC runtime.
//
// The SymCC runtime is free software: you can redistribute it and/or modify it
// under the terms of the GNU Lesser General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version.
//
// The SymCC runtime is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
// for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with the SymCC runtime. If not, see <https://www.gnu.org/licenses/>.

#ifndef RUNTIMECOMMON_H
#define RUNTIMECOMMON_H

/* Marker for expression parameters which may be null. */
#define nullable

#ifdef __cplusplus
#include <cstddef>
#include <cstdint>
extern "C" {
#else
#include <stddef.h>
#include <stdint.h>
#endif

/*
 * Initialization
 */
void _sym_initialize(void);

/*
 * Construction of simple values
 */
SymExpr _sym_build_integer(uint64_t value, uint8_t bits);
SymExpr _sym_build_integer128(uint64_t high, uint64_t low);
SymExpr _sym_build_float(double value, int is_double);
SymExpr _sym_build_null_pointer(void);
SymExpr _sym_build_true(void);
SymExpr _sym_build_false(void);
SymExpr _sym_build_bool(bool value);

/*
 * Integer arithmetic and shifts
 */
SymExpr _sym_build_neg(SymExpr expr);
SymExpr _sym_build_add(SymExpr a, SymExpr b);
SymExpr _sym_build_sub(SymExpr a, SymExpr b);
SymExpr _sym_build_mul(SymExpr a, SymExpr b);
SymExpr _sym_build_unsigned_div(SymExpr a, SymExpr b);
SymExpr _sym_build_signed_div(SymExpr a, SymExpr b);
SymExpr _sym_build_unsigned_rem(SymExpr a, SymExpr b);
SymExpr _sym_build_signed_rem(SymExpr a, SymExpr b);
SymExpr _sym_build_shift_left(SymExpr a, SymExpr b);
SymExpr _sym_build_logical_shift_right(SymExpr a, SymExpr b);
SymExpr _sym_build_arithmetic_shift_right(SymExpr a, SymExpr b);
SymExpr _sym_build_funnel_shift_left(SymExpr a, SymExpr b, SymExpr c);
SymExpr _sym_build_funnel_shift_right(SymExpr a, SymExpr b, SymExpr c);
SymExpr _sym_build_abs(SymExpr expr);

/*
 * Arithmetic with overflow
 */
SymExpr _sym_build_add_overflow(SymExpr a, SymExpr b, bool is_signed,
                                bool little_endian);
SymExpr _sym_build_sub_overflow(SymExpr a, SymExpr b, bool is_signed,
                                bool little_endian);
SymExpr _sym_build_mul_overflow(SymExpr a, SymExpr b, bool is_signed,
                                bool little_endian);

/*
 * Saturating integer arithmetic and shifts
 */
SymExpr _sym_build_sadd_sat(SymExpr a, SymExpr b);
SymExpr _sym_build_uadd_sat(SymExpr a, SymExpr b);
SymExpr _sym_build_ssub_sat(SymExpr a, SymExpr b);
SymExpr _sym_build_usub_sat(SymExpr a, SymExpr b);
SymExpr _sym_build_sshl_sat(SymExpr a, SymExpr b);
SymExpr _sym_build_ushl_sat(SymExpr a, SymExpr b);

/*
 * Floating-point arithmetic and shifts
 */
SymExpr _sym_build_fp_add(SymExpr a, SymExpr b);
SymExpr _sym_build_fp_sub(SymExpr a, SymExpr b);
SymExpr _sym_build_fp_mul(SymExpr a, SymExpr b);
SymExpr _sym_build_fp_div(SymExpr a, SymExpr b);
SymExpr _sym_build_fp_rem(SymExpr a, SymExpr b);
SymExpr _sym_build_fp_abs(SymExpr a);
SymExpr _sym_build_fp_neg(SymExpr a);

/*
 * Boolean operations
 */
SymExpr _sym_build_not(SymExpr expr);
SymExpr _sym_build_signed_less_than(SymExpr a, SymExpr b);
SymExpr _sym_build_signed_less_equal(SymExpr a, SymExpr b);
SymExpr _sym_build_signed_greater_than(SymExpr a, SymExpr b);
SymExpr _sym_build_signed_greater_equal(SymExpr a, SymExpr b);
SymExpr _sym_build_unsigned_less_than(SymExpr a, SymExpr b);
SymExpr _sym_build_unsigned_less_equal(SymExpr a, SymExpr b);
SymExpr _sym_build_unsigned_greater_than(SymExpr a, SymExpr b);
SymExpr _sym_build_unsigned_greater_equal(SymExpr a, SymExpr b);
SymExpr _sym_build_equal(SymExpr a, SymExpr b);
SymExpr _sym_build_not_equal(SymExpr a, SymExpr b);
SymExpr _sym_build_bool_and(SymExpr a, SymExpr b);
SymExpr _sym_build_and(SymExpr a, SymExpr b);
SymExpr _sym_build_bool_or(SymExpr a, SymExpr b);
SymExpr _sym_build_or(SymExpr a, SymExpr b);
SymExpr _sym_build_bool_xor(SymExpr a, SymExpr b);
SymExpr _sym_build_xor(SymExpr a, SymExpr b);
SymExpr _sym_build_ite(SymExpr cond, SymExpr a, SymExpr b);

SymExpr _sym_build_float_ordered_greater_than(SymExpr a, SymExpr b);
SymExpr _sym_build_float_ordered_greater_equal(SymExpr a, SymExpr b);
SymExpr _sym_build_float_ordered_less_than(SymExpr a, SymExpr b);
SymExpr _sym_build_float_ordered_less_equal(SymExpr a, SymExpr b);
SymExpr _sym_build_float_ordered_equal(SymExpr a, SymExpr b);
SymExpr _sym_build_float_ordered_not_equal(SymExpr a, SymExpr b);
SymExpr _sym_build_float_ordered(SymExpr a, SymExpr b);
SymExpr _sym_build_float_unordered(SymExpr a, SymExpr b);
SymExpr _sym_build_float_unordered_greater_than(SymExpr a, SymExpr b);
SymExpr _sym_build_float_unordered_greater_equal(SymExpr a, SymExpr b);
SymExpr _sym_build_float_unordered_less_than(SymExpr a, SymExpr b);
SymExpr _sym_build_float_unordered_less_equal(SymExpr a, SymExpr b);
SymExpr _sym_build_float_unordered_equal(SymExpr a, SymExpr b);
SymExpr _sym_build_float_unordered_not_equal(SymExpr a, SymExpr b);

/*
 * Casts
 */
SymExpr _sym_build_sext(nullable SymExpr expr, uint8_t bits);
SymExpr _sym_build_zext(nullable SymExpr expr, uint8_t bits);
SymExpr _sym_build_trunc(nullable SymExpr expr, uint8_t bits);
SymExpr _sym_build_bswap(SymExpr expr);
SymExpr _sym_build_int_to_float(SymExpr value, int is_double, int is_signed);
SymExpr _sym_build_float_to_float(SymExpr expr, int to_double);
SymExpr _sym_build_bits_to_float(SymExpr expr, int to_double);
SymExpr _sym_build_float_to_bits(SymExpr expr);
SymExpr _sym_build_float_to_signed_integer(SymExpr expr, uint8_t bits);
SymExpr _sym_build_float_to_unsigned_integer(SymExpr expr, uint8_t bits);
SymExpr _sym_build_bool_to_bit(nullable SymExpr expr);
SymExpr _sym_build_bit_to_bool(nullable SymExpr expr);

/*
 * Bit-array helpers
 */
SymExpr _sym_concat_helper(SymExpr a, SymExpr b);
SymExpr _sym_extract_helper(SymExpr expr, size_t first_bit, size_t last_bit);
size_t _sym_bits_helper(SymExpr expr);

/*
 * Function-call helpers
 */
void _sym_set_parameter_expression(uint8_t index, nullable SymExpr expr);
SymExpr _sym_get_parameter_expression(uint8_t index);
void _sym_set_return_expression(nullable SymExpr expr);
SymExpr _sym_get_return_expression(void);

/*
 * Constraint handling
 */
void _sym_push_path_constraint(nullable SymExpr constraint, int taken,
                               uintptr_t site_id);
SymExpr _sym_get_input_byte(size_t offset, uint8_t concrete_value);
void _sym_make_symbolic(const void *data, size_t byte_length,
                        size_t input_offset);

/*
 * Memory management
 */
SymExpr _sym_read_memory(uint8_t *addr, size_t length, bool little_endian);
void _sym_write_memory(uint8_t *addr, size_t length, nullable SymExpr expr,
                       bool little_endian);
void _sym_memcpy(uint8_t *dest, const uint8_t *src, size_t length);
void _sym_memset(uint8_t *memory, SymExpr value, size_t length);
void _sym_memmove(uint8_t *dest, const uint8_t *src, size_t length);
SymExpr _sym_build_zero_bytes(size_t length);
SymExpr _sym_build_insert(SymExpr target, SymExpr to_insert, uint64_t offset,
                          bool little_endian);
SymExpr _sym_build_extract(SymExpr expr, uint64_t offset, uint64_t length,
                           bool little_endian);

/*
 * Call-stack tracing
 */
void _sym_notify_call(uintptr_t site_id);
void _sym_notify_ret(uintptr_t site_id);
void _sym_notify_basic_block(uintptr_t site_id);

/*
 * Debugging
 */
const char *_sym_expr_to_string(SymExpr expr); // statically allocated
bool _sym_feasible(SymExpr expr);

/*
 * Garbage collection
 */
void _sym_register_expression_region(SymExpr *start, size_t length);
void _sym_collect_garbage(void);

/*
 * User-facing functionality
 *
 * These are the only functions in the interface that we expect to be called by
 * users (i.e., calls to it aren't auto-generated by our compiler pass).
 */
void symcc_make_symbolic(const void *start, size_t byte_length);
typedef void (*TestCaseHandler)(const void *, size_t);
void symcc_set_test_case_handler(TestCaseHandler handler);

#ifdef __cplusplus
}
#endif

#undef nullable

#endif

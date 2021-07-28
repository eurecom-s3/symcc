// Rust Run-time library interface                                  -*- C++ -*-
//
// This header is mostly a straight copy of RuntimeCommon.h with different
// function name prefixes, a separate SymExpr type and all functions that are
// implemented by this wrapper removed. This file defines the interface that the
// wrapped runtime should implement. Consult the README for a high-level
// overview.
//
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

#ifndef RUSTRUNTIME_H
#define RUSTRUNTIME_H

#include <stddef.h>

#ifdef __cplusplus
#include <cstdint>
extern "C" {
#else
#include <stdint.h>
#endif

typedef uintptr_t RSymExpr;

/*
 * Construction of simple values
 */
RSymExpr _rsym_build_integer(uint64_t value, uint8_t bits);
RSymExpr _rsym_build_integer128(uint64_t high, uint64_t low);
RSymExpr _rsym_build_float(double value, bool is_double);
RSymExpr _rsym_build_null_pointer(void);
RSymExpr _rsym_build_true(void);
RSymExpr _rsym_build_false(void);
RSymExpr _rsym_build_bool(bool value);

/*
 * Arithmetic and shifts
 */
RSymExpr _rsym_build_neg(RSymExpr expr);
RSymExpr _rsym_build_add(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_sub(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_mul(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_unsigned_div(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_signed_div(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_unsigned_rem(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_signed_rem(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_shift_left(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_logical_shift_right(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_arithmetic_shift_right(RSymExpr a, RSymExpr b);

RSymExpr _rsym_build_fp_add(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_fp_sub(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_fp_mul(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_fp_div(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_fp_rem(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_fp_abs(RSymExpr a);

/*
 * Boolean operations
 */
RSymExpr _rsym_build_not(RSymExpr expr);
RSymExpr _rsym_build_signed_less_than(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_signed_less_equal(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_signed_greater_than(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_signed_greater_equal(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_unsigned_less_than(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_unsigned_less_equal(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_unsigned_greater_than(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_unsigned_greater_equal(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_equal(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_not_equal(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_bool_and(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_and(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_bool_or(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_or(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_bool_xor(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_xor(RSymExpr a, RSymExpr b);

RSymExpr _rsym_build_float_ordered_greater_than(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_float_ordered_greater_equal(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_float_ordered_less_than(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_float_ordered_less_equal(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_float_ordered_equal(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_float_ordered_not_equal(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_float_ordered(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_float_unordered(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_float_unordered_greater_than(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_float_unordered_greater_equal(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_float_unordered_less_than(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_float_unordered_less_equal(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_float_unordered_equal(RSymExpr a, RSymExpr b);
RSymExpr _rsym_build_float_unordered_not_equal(RSymExpr a, RSymExpr b);

/*
 * Casts
 */
RSymExpr _rsym_build_sext(RSymExpr expr, uint8_t bits);
RSymExpr _rsym_build_zext(RSymExpr expr, uint8_t bits);
RSymExpr _rsym_build_trunc(RSymExpr expr, uint8_t bits);
RSymExpr _rsym_build_int_to_float(RSymExpr value, bool is_double,
                                  bool is_signed);
RSymExpr _rsym_build_float_to_float(RSymExpr expr, bool to_double);
RSymExpr _rsym_build_bits_to_float(RSymExpr expr, bool to_double);
RSymExpr _rsym_build_float_to_bits(RSymExpr expr);
RSymExpr _rsym_build_float_to_signed_integer(RSymExpr expr, uint8_t bits);
RSymExpr _rsym_build_float_to_unsigned_integer(RSymExpr expr, uint8_t bits);
RSymExpr _rsym_build_bool_to_bits(RSymExpr expr, uint8_t bits);

/*
 * Bit-array helpers
 */
RSymExpr _rsym_concat_helper(RSymExpr a, RSymExpr b);
RSymExpr _rsym_extract_helper(RSymExpr expr, size_t first_bit, size_t last_bit);

/*
 * Constraint handling
 */
void _rsym_push_path_constraint(RSymExpr constraint, bool taken,
                                uintptr_t site_id);
RSymExpr _rsym_get_input_byte(size_t offset);

/*
 * Call-stack tracing
 */
void _rsym_notify_call(uintptr_t site_id);
void _rsym_notify_ret(uintptr_t site_id);
void _rsym_notify_basic_block(uintptr_t site_id);

/*
 * Garbage collection
 */
void _rsym_expression_unreachable(RSymExpr *expressions, size_t num_elements);

#ifdef __cplusplus
}
#endif

#endif

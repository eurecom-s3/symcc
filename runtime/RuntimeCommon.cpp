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
// along with SymCC. If not, see <https://www.gnu.org/licenses/>.

#include <Runtime.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <numeric>
#include <stdexcept>
#include <variant>

#include "Config.h"
#include "GarbageCollection.h"
#include "RuntimeCommon.h"
#include "Shadow.h"

namespace {

constexpr int kMaxFunctionArguments = 256;

/// Global storage for function parameters and the return value.
SymExpr g_return_value;
std::array<SymExpr, kMaxFunctionArguments> g_function_arguments;
// TODO make thread-local

SymExpr buildMinSignedInt(uint8_t bits) {
  return _sym_build_integer((uint64_t)(1) << (bits - 1), bits);
}

SymExpr buildMaxSignedInt(uint8_t bits) {
  uint64_t mask = ((uint64_t)(1) << bits) - 1;
  return _sym_build_integer(((uint64_t)(~0) & mask) >> 1, bits);
}

SymExpr buildMaxUnsignedInt(uint8_t bits) {
  uint64_t mask = ((uint64_t)(1) << bits) - 1;
  return _sym_build_integer((uint64_t)(~0) & mask, bits);
}

/// Construct an expression describing the in-memory representation of the
/// bitcode structure {iN, i1} returned by the intrinsics for arithmetic with
/// overflow (see
/// https://llvm.org/docs/LangRef.html#arithmetic-with-overflow-intrinsics). The
/// overflow parameter is expected to be a symbolic Boolean.
SymExpr buildOverflowResult(SymExpr result_expr, SymExpr overflow,
                            bool little_endian) {
  auto result_bits = _sym_bits_helper(result_expr);
  assert(result_bits % 8 == 0 &&
         "Arithmetic with overflow on integers of invalid length");

  // When storing {iN, i1} in memory, the compiler would insert padding between
  // the two elements, extending the Boolean to the same size as the integer. We
  // simulate the same here, taking endianness into account.

  auto result_expr_mem =
      little_endian ? _sym_build_bswap(result_expr) : result_expr;
  auto overflow_byte = _sym_build_zext(_sym_build_bool_to_bit(overflow), 7);

  // There's no padding if the result is a single byte.
  if (result_bits == 8) {
    return _sym_concat_helper(result_expr_mem, overflow_byte);
  }

  auto padding = _sym_build_zero_bytes(result_bits / 8 - 1);
  return _sym_concat_helper(result_expr_mem,
                            little_endian
                                ? _sym_concat_helper(overflow_byte, padding)
                                : _sym_concat_helper(padding, overflow_byte));
}

} // namespace

void _sym_set_return_expression(SymExpr expr) { g_return_value = expr; }

SymExpr _sym_get_return_expression(void) {
  auto *result = g_return_value;
  // TODO this is a safeguard that can eventually be removed
  g_return_value = nullptr;
  return result;
}

void _sym_set_parameter_expression(uint8_t index, SymExpr expr) {
  g_function_arguments[index] = expr;
}

SymExpr _sym_get_parameter_expression(uint8_t index) {
  return g_function_arguments[index];
}

void _sym_memcpy(uint8_t *dest, const uint8_t *src, size_t length) {
  if (isConcrete(src, length) && isConcrete(dest, length))
    return;

  ReadOnlyShadow srcShadow(src, length);
  ReadWriteShadow destShadow(dest, length);
  std::copy(srcShadow.begin(), srcShadow.end(), destShadow.begin());
}

void _sym_memset(uint8_t *memory, SymExpr value, size_t length) {
  if ((value == nullptr) && isConcrete(memory, length))
    return;

  ReadWriteShadow shadow(memory, length);
  std::fill(shadow.begin(), shadow.end(), value);
}

void _sym_memmove(uint8_t *dest, const uint8_t *src, size_t length) {
  // Unless both the source and the destination are fully concrete memory
  // regions, we need to copy the symbolic expressions over. (In the case where
  // only the destination is symbolic, this means making it concrete.)

  if (isConcrete(src, length) && isConcrete(dest, length))
    return;

  ReadOnlyShadow srcShadow(src, length);
  ReadWriteShadow destShadow(dest, length);
  if (dest > src)
    std::copy_backward(srcShadow.begin(), srcShadow.end(), destShadow.end());
  else
    std::copy(srcShadow.begin(), srcShadow.end(), destShadow.begin());
}

SymExpr _sym_read_memory(uint8_t *addr, size_t length, bool little_endian) {
  assert(length && "Invalid query for zero-length memory region");

#ifdef DEBUG_RUNTIME
  std::cerr << "Reading " << length << " bytes from address " << P(addr)
            << std::endl;
  dump_known_regions();
#endif

  // If the entire memory region is concrete, don't create a symbolic expression
  // at all.
  if (isConcrete(addr, length))
    return nullptr;

  ReadOnlyShadow shadow(addr, length);
  return std::accumulate(shadow.begin_non_null(), shadow.end_non_null(),
                         static_cast<SymExpr>(nullptr),
                         [&](SymExpr result, SymExpr byteExpr) {
                           if (result == nullptr)
                             return byteExpr;

                           return little_endian
                                      ? _sym_concat_helper(byteExpr, result)
                                      : _sym_concat_helper(result, byteExpr);
                         });
}

void _sym_write_memory(uint8_t *addr, size_t length, SymExpr expr,
                       bool little_endian) {
  assert(length && "Invalid query for zero-length memory region");

#ifdef DEBUG_RUNTIME
  std::cerr << "Writing " << length << " bytes to address " << P(addr)
            << std::endl;
  dump_known_regions();
#endif

  if (expr == nullptr && isConcrete(addr, length))
    return;

  ReadWriteShadow shadow(addr, length);
  if (expr == nullptr) {
    std::fill(shadow.begin(), shadow.end(), nullptr);
  } else {
    size_t i = 0;
    for (SymExpr &byteShadow : shadow) {
      byteShadow = little_endian
                       ? _sym_extract_helper(expr, 8 * (i + 1) - 1, 8 * i)
                       : _sym_extract_helper(expr, (length - i) * 8 - 1,
                                             (length - i - 1) * 8);
      i++;
    }
  }
}

SymExpr _sym_build_extract(SymExpr expr, uint64_t offset, uint64_t length,
                           bool little_endian) {
  size_t totalBits = _sym_bits_helper(expr);
  assert((totalBits % 8 == 0) && "Aggregate type contains partial bytes");

  SymExpr result;
  if (little_endian) {
    result = _sym_extract_helper(expr, totalBits - offset * 8 - 1,
                                 totalBits - offset * 8 - 8);
    for (size_t i = 1; i < length; i++) {
      result = _sym_concat_helper(
          _sym_extract_helper(expr, totalBits - (offset + i) * 8 - 1,
                              totalBits - (offset + i + 1) * 8),
          result);
    }
  } else {
    result = _sym_extract_helper(expr, totalBits - offset * 8 - 1,
                                 totalBits - (offset + length) * 8);
  }

  return result;
}

SymExpr _sym_build_bswap(SymExpr expr) {
  size_t bits = _sym_bits_helper(expr);
  assert((bits % 16 == 0) && "bswap is not applicable");
  return _sym_build_extract(expr, 0, bits / 8, true);
}

SymExpr _sym_build_insert(SymExpr target, SymExpr to_insert, uint64_t offset,
                          bool little_endian) {
  size_t bitsToInsert = _sym_bits_helper(to_insert);
  assert((bitsToInsert % 8 == 0) &&
         "Expression to insert contains partial bytes");

  SymExpr beforeInsert =
      (offset == 0) ? nullptr : _sym_build_extract(target, 0, offset, false);
  SymExpr newPiece = (little_endian && bitsToInsert > 8)
                         ? _sym_build_bswap(to_insert)
                         : to_insert;
  uint64_t afterLen =
      (_sym_bits_helper(target) / 8) - offset - (bitsToInsert / 8);
  SymExpr afterInsert =
      (afterLen == 0) ? nullptr
                      : _sym_build_extract(target, offset + (bitsToInsert / 8),
                                           afterLen, false);

  SymExpr result = beforeInsert;
  if (result == nullptr) {
    result = newPiece;
  } else {
    result = _sym_concat_helper(result, newPiece);
  }

  if (afterInsert != nullptr) {
    result = _sym_concat_helper(result, afterInsert);
  }

  return result;
}

SymExpr _sym_build_zero_bytes(size_t length) {
  auto zero_byte = _sym_build_integer(0, 8);

  auto result = zero_byte;
  for (size_t i = 1; i < length; i++) {
    result = _sym_concat_helper(result, zero_byte);
  }

  return result;
}

SymExpr _sym_build_sadd_sat(SymExpr a, SymExpr b) {
  size_t bits = _sym_bits_helper(a);
  SymExpr min = buildMinSignedInt(bits);
  SymExpr max = buildMaxSignedInt(bits);
  SymExpr add_sext =
      _sym_build_add(_sym_build_sext(a, 1), _sym_build_sext(b, 1));

  return _sym_build_ite(
      // If the result is less than the min signed integer...
      _sym_build_signed_less_equal(add_sext, _sym_build_sext(min, 1)),
      // ... Return the min signed integer
      min,
      _sym_build_ite(
          // Otherwise, if the result is greater than the max signed integer...
          _sym_build_signed_greater_equal(add_sext, _sym_build_sext(max, 1)),
          // ... Return the max signed integer
          max,
          // Otherwise, return the addition
          _sym_build_add(a, b)));
}

SymExpr _sym_build_uadd_sat(SymExpr a, SymExpr b) {
  size_t bits = _sym_bits_helper(a);
  SymExpr max = buildMaxUnsignedInt(bits);
  SymExpr add_zext =
      _sym_build_add(_sym_build_zext(a, 1), _sym_build_zext(b, 1));

  return _sym_build_ite(
      // If the top bit is set, an overflow has occurred and...
      _sym_build_bit_to_bool(_sym_extract_helper(add_zext, bits, bits)),
      // ... Return the max unsigned integer
      max,
      // Otherwise, return the addition
      _sym_build_add(a, b));
}

SymExpr _sym_build_ssub_sat(SymExpr a, SymExpr b) {
  size_t bits = _sym_bits_helper(a);
  SymExpr min = buildMinSignedInt(bits);
  SymExpr max = buildMaxSignedInt(bits);

  SymExpr sub_sext =
      _sym_build_sub(_sym_build_sext(a, 1), _sym_build_sext(b, 1));

  return _sym_build_ite(
      // If the result is less than the min signed integer...
      _sym_build_signed_less_equal(sub_sext, _sym_build_sext(min, 1)),
      // ... Return the min signed integer
      min,
      _sym_build_ite(
          // Otherwise, if the result is greater than the max signed integer...
          _sym_build_signed_greater_equal(sub_sext, _sym_build_sext(max, 1)),
          // ... Return the max signed integer
          max,
          // Otherwise, return the subtraction
          _sym_build_sub(a, b)));
}

SymExpr _sym_build_usub_sat(SymExpr a, SymExpr b) {
  size_t bits = _sym_bits_helper(a);

  return _sym_build_ite(
      // If `a >= b`, then no overflow occurs and...
      _sym_build_unsigned_greater_equal(a, b),
      // ... Return the subtraction
      _sym_build_sub(a, b),
      // Otherwise, saturate at zero
      _sym_build_integer(0, bits));
}

static SymExpr _sym_build_shift_left_overflow(SymExpr a, SymExpr b) {
  return _sym_build_not_equal(
      _sym_build_arithmetic_shift_right(_sym_build_shift_left(a, b), b), a);
}

SymExpr _sym_build_sshl_sat(SymExpr a, SymExpr b) {
  size_t bits = _sym_bits_helper(a);

  return _sym_build_ite(
      // If an overflow occurred...
      _sym_build_shift_left_overflow(a, b),
      _sym_build_ite(
          // ... And the LHS is negative...
          _sym_build_bit_to_bool(_sym_extract_helper(a, bits - 1, bits - 1)),
          // ... Return the min signed integer...
          buildMinSignedInt(bits),
          // ... Otherwise, return the max signed integer
          buildMaxSignedInt(bits)),
      // Otherwise, return the left shift
      _sym_build_shift_left(a, b));
}

SymExpr _sym_build_ushl_sat(SymExpr a, SymExpr b) {
  size_t bits = _sym_bits_helper(a);

  return _sym_build_ite(
      // If an overflow occurred...
      _sym_build_shift_left_overflow(a, b),
      // ... Return the max unsigned integer
      buildMaxUnsignedInt(bits),
      // Otherwise, return the left shift
      _sym_build_shift_left(a, b));
}

SymExpr _sym_build_add_overflow(SymExpr a, SymExpr b, bool is_signed,
                                bool little_endian) {
  size_t bits = _sym_bits_helper(a);
  SymExpr overflow = [&]() {
    if (is_signed) {
      // Check if the additions are different
      SymExpr add_sext =
          _sym_build_add(_sym_build_sext(a, 1), _sym_build_sext(b, 1));
      return _sym_build_not_equal(add_sext,
                                  _sym_build_sext(_sym_build_add(a, b), 1));
    } else {
      // Check if the addition overflowed into the extra bit
      SymExpr add_zext =
          _sym_build_add(_sym_build_zext(a, 1), _sym_build_zext(b, 1));
      return _sym_build_equal(_sym_extract_helper(add_zext, bits, bits),
                              _sym_build_true());
    }
  }();

  return buildOverflowResult(_sym_build_add(a, b), overflow, little_endian);
}

SymExpr _sym_build_sub_overflow(SymExpr a, SymExpr b, bool is_signed,
                                bool little_endian) {
  size_t bits = _sym_bits_helper(a);
  SymExpr overflow = [&]() {
    if (is_signed) {
      // Check if the subtractions are different
      SymExpr sub_sext =
          _sym_build_sub(_sym_build_sext(a, 1), _sym_build_sext(b, 1));
      return _sym_build_not_equal(sub_sext,
                                  _sym_build_sext(_sym_build_sub(a, b), 1));
    } else {
      // Check if the subtraction overflowed into the extra bit
      SymExpr sub_zext =
          _sym_build_sub(_sym_build_zext(a, 1), _sym_build_zext(b, 1));
      return _sym_build_equal(_sym_extract_helper(sub_zext, bits, bits),
                              _sym_build_true());
    }
  }();

  return buildOverflowResult(_sym_build_sub(a, b), overflow, little_endian);
}

SymExpr _sym_build_mul_overflow(SymExpr a, SymExpr b, bool is_signed,
                                bool little_endian) {
  size_t bits = _sym_bits_helper(a);
  SymExpr overflow = [&]() {
    if (is_signed) {
      // Check if the multiplications are different
      SymExpr mul_sext =
          _sym_build_mul(_sym_build_sext(a, bits), _sym_build_sext(b, bits));
      return _sym_build_not_equal(mul_sext,
                                  _sym_build_sext(_sym_build_mul(a, b), bits));
    } else {
      // Check if the multiplication overflowed into the extra bit
      SymExpr mul_zext =
          _sym_build_mul(_sym_build_zext(a, bits), _sym_build_zext(b, bits));
      return _sym_build_equal(
          _sym_extract_helper(mul_zext, 2 * bits - 1, 2 * bits - 1),
          _sym_build_true());
    }
  }();

  return buildOverflowResult(_sym_build_mul(a, b), overflow, little_endian);
}

SymExpr _sym_build_funnel_shift_left(SymExpr a, SymExpr b, SymExpr c) {
  size_t bits = _sym_bits_helper(c);
  SymExpr concat = _sym_concat_helper(a, b);
  SymExpr shift = _sym_build_unsigned_rem(c, _sym_build_integer(bits, bits));

  return _sym_extract_helper(_sym_build_shift_left(concat, shift), 0, bits);
}

SymExpr _sym_build_funnel_shift_right(SymExpr a, SymExpr b, SymExpr c) {
  size_t bits = _sym_bits_helper(c);
  SymExpr concat = _sym_concat_helper(a, b);
  SymExpr shift = _sym_build_unsigned_rem(c, _sym_build_integer(bits, bits));

  return _sym_extract_helper(_sym_build_logical_shift_right(concat, shift), 0,
                             bits);
}

SymExpr _sym_build_abs(SymExpr expr) {
  size_t bits = _sym_bits_helper(expr);
  return _sym_build_ite(
      _sym_build_signed_greater_equal(expr, _sym_build_integer(0, bits)), expr,
      _sym_build_sub(_sym_build_integer(0, bits), expr));
}

void _sym_register_expression_region(SymExpr *start, size_t length) {
  registerExpressionRegion({start, length});
}

void _sym_make_symbolic(const void *data, size_t byte_length,
                        size_t input_offset) {
  ReadWriteShadow shadow(data, byte_length);
  const uint8_t *data_bytes = reinterpret_cast<const uint8_t *>(data);
  std::generate(shadow.begin(), shadow.end(), [&, i = 0]() mutable {
    return _sym_get_input_byte(input_offset++, data_bytes[i++]);
  });
}

void symcc_make_symbolic(const void *start, size_t byte_length) {
  if (!std::holds_alternative<MemoryInput>(g_config.input))
    throw std::runtime_error{"Calls to symcc_make_symbolic aren't allowed when "
                             "SYMCC_MEMORY_INPUT isn't set"};

  static size_t inputOffset = 0; // track the offset across calls
  _sym_make_symbolic(start, byte_length, inputOffset);
  inputOffset += byte_length;
}

SymExpr _sym_build_bit_to_bool(SymExpr expr) {
  if (expr == nullptr)
    return nullptr;

  return _sym_build_not_equal(expr,
                              _sym_build_integer(0, _sym_bits_helper(expr)));
}

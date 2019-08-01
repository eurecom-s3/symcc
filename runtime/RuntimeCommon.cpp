#include <Runtime.h>

#include "Shadow.h"

namespace {

constexpr int kMaxFunctionArguments = 256;

/// Global storage for function parameters and the return value.
SymExpr g_return_value;
SymExpr g_function_arguments[kMaxFunctionArguments];
// TODO make thread-local

} // namespace

void _sym_set_return_expression(SymExpr expr) { g_return_value = expr; }

SymExpr _sym_get_return_expression(void) {
  auto result = g_return_value;
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

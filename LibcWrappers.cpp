#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <sys/mman.h>
#include <unistd.h>

#include "Runtime.h"

#define SYM(x) __symbolized_##x

// #define DEBUG_RUNTIME
#ifdef DEBUG_RUNTIME
#include <iostream>
#endif

namespace {
/// Get the shadow for addr and assert that it covers at least n bytes.
Z3_ast *getShadow(const void *addr, size_t n) {
  auto region = _sym_get_memory_region(addr);
  auto byteBuf = static_cast<const uint8_t *>(addr);
  assert(region && (byteBuf + n <= region->end) && "Unknown memory region");

  return (region->shadow + (byteBuf - region->start));
}

/// Tell the solver to try an alternative value than the given one.
template <typename V> void tryAlternative(V value, Z3_ast valueExpr) {
  if (valueExpr) {
    _sym_push_path_constraint(
        _sym_build_equal(valueExpr,
                         _sym_build_integer(value, sizeof(value) * 8)),
        true);
  }
}

// A partial specialization for pointer types for convenience.
template <typename E> void tryAlternative(E *value, Z3_ast valueExpr) {
  tryAlternative(reinterpret_cast<intptr_t>(value), valueExpr);
}
} // namespace

extern "C" {

void *SYM(malloc)(size_t size) {
  auto result = malloc(size);

  auto shadow = static_cast<Z3_ast *>(malloc(size * sizeof(Z3_ast)));
  _sym_register_memory(static_cast<uint8_t *>(result), shadow, size);

  tryAlternative(size, _sym_get_parameter_expression(0));

  _sym_set_return_expression(nullptr);
  return result;
}

void *SYM(mmap)(void *addr, size_t len, int prot, int flags, int fildes,
                off_t off) {
  auto result = mmap(addr, len, prot, flags, fildes, off);

  tryAlternative(len, _sym_get_parameter_expression(1));

  auto shadow = static_cast<Z3_ast *>(malloc(len * sizeof(Z3_ast)));
  _sym_register_memory(static_cast<uint8_t *>(result), shadow, len);

  _sym_set_return_expression(nullptr);
  return result;
}

ssize_t SYM(read)(int fildes, void *buf, size_t nbyte) {
  static std::vector<Z3_ast> stdinBytes;

  tryAlternative(buf, _sym_get_parameter_expression(1));
  tryAlternative(nbyte, _sym_get_parameter_expression(2));

  auto result = read(fildes, buf, nbyte);
  auto shadowStart = getShadow(buf, nbyte);
  if (fildes == 0) {
    // Reading from standard input. We treat everything as unconstrained
    // symbolic data.
    for (int index = 0; index < result; index++) {
      auto varName = "stdin" + std::to_string(stdinBytes.size());
      auto var = _sym_build_variable(varName.c_str(), 8);
      stdinBytes.push_back(var);
      shadowStart[index] = var;
    }
  } else {
    _sym_initialize_memory(static_cast<uint8_t *>(buf), shadowStart, nbyte);
  }

  _sym_set_return_expression(nullptr);
  return result;
}

void *SYM(memcpy)(void *dest, const void *src, size_t n) {
  auto result = memcpy(dest, src, n);

  tryAlternative(dest, _sym_get_parameter_expression(0));
  tryAlternative(src, _sym_get_parameter_expression(1));
  tryAlternative(n, _sym_get_parameter_expression(2));

  _sym_memcpy(static_cast<uint8_t *>(dest), static_cast<const uint8_t *>(src),
              n);
  _sym_set_return_expression(_sym_get_parameter_expression(0));
  return result;
}

void *SYM(memset)(void *s, int c, size_t n) {
  auto result = memset(s, c, n);

  tryAlternative(s, _sym_get_parameter_expression(0));
  tryAlternative(n, _sym_get_parameter_expression(2));

  _sym_memset(static_cast<uint8_t *>(s), _sym_get_parameter_expression(1), n);
  _sym_set_return_expression(_sym_get_parameter_expression(0));
  return result;
}

char *SYM(strncpy)(char *dest, const char *src, size_t n) {
  tryAlternative(dest, _sym_get_parameter_expression(0));
  tryAlternative(src, _sym_get_parameter_expression(1));
  tryAlternative(n, _sym_get_parameter_expression(2));

  auto result = strncpy(dest, src, n);
  size_t srcLen = strnlen(src, n);

  auto srcShadow = getShadow(src, srcLen);
  auto destShadow = getShadow(dest, n);

  for (size_t i = 0; i < srcLen; i++) {
    destShadow[i] = srcShadow[i];
  }
  for (size_t i = srcLen; i < n; i++) {
    destShadow[i] = 0;
  }

  _sym_set_return_expression(nullptr);
  return result;
}

const char *SYM(strchr)(const char *s, int c) {
  tryAlternative(s, _sym_get_parameter_expression(0));
  tryAlternative(c, _sym_get_parameter_expression(1));

  auto result = strchr(s, c);

  Z3_ast cExpr = _sym_get_parameter_expression(1);
  if (!cExpr)
    cExpr = _sym_build_integer(c, 8);

  size_t length = result ? (result - s) : strlen(s);
  auto shadow = getShadow(s, length);
  for (size_t i = 0; i < length; i++) {
    _sym_push_path_constraint(_sym_build_not_equal(shadow[i], cExpr), true);
  }

  _sym_set_return_expression(nullptr);
  return result;
}

int SYM(memcmp)(const void *a, const void *b, size_t n) {
  tryAlternative(a, _sym_get_parameter_expression(0));
  tryAlternative(b, _sym_get_parameter_expression(1));
  tryAlternative(n, _sym_get_parameter_expression(2));

  auto result = memcmp(a, b, n);

  auto aShadow = getShadow(a, n);
  auto bShadow = getShadow(b, n);
  Z3_ast allEqual = _sym_build_equal(aShadow[0], bShadow[0]);
  for (size_t i = 1; i < n; i++) {
    allEqual =
        _sym_build_and(allEqual, _sym_build_equal(aShadow[i], bShadow[i]));
  }

  _sym_push_path_constraint(allEqual, result ? false : true);
  return result;
}
}

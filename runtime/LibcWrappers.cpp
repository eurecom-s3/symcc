#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <sys/mman.h>
#include <unistd.h>

#include "Runtime.h"
#include "Shadow.h"

#define SYM(x) __symbolized_##x

// #define DEBUG_RUNTIME
#ifdef DEBUG_RUNTIME
#include <iostream>
#endif

namespace {

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

  tryAlternative(size, _sym_get_parameter_expression(0));

  _sym_set_return_expression(nullptr);
  return result;
}

void *SYM(mmap)(void *addr, size_t len, int prot, int flags, int fildes,
                off_t off) {
  auto result = mmap(addr, len, prot, flags, fildes, off);

  tryAlternative(len, _sym_get_parameter_expression(1));

  _sym_set_return_expression(nullptr);
  return result;
}

ssize_t SYM(read)(int fildes, void *buf, size_t nbyte) {
  static std::vector<Z3_ast> stdinBytes;

  tryAlternative(buf, _sym_get_parameter_expression(1));
  tryAlternative(nbyte, _sym_get_parameter_expression(2));

  auto result = read(fildes, buf, nbyte);
  if (fildes == 0) {
    // Reading from standard input. We treat everything as unconstrained
    // symbolic data.
    ReadWriteShadow shadow(buf, nbyte);
    std::generate(shadow.begin(), shadow.end(), []() {
      auto varName = "stdin" + std::to_string(stdinBytes.size());
      auto var = _sym_build_variable(varName.c_str(), 8);
      stdinBytes.push_back(var);
      return var;
    });
  } else if (!isConcrete(buf, nbyte)) {
    ReadWriteShadow shadow(buf, nbyte);
    std::fill(shadow.begin(), shadow.end(), nullptr);
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
  _sym_set_return_expression(nullptr);

  size_t srcLen = strnlen(src, n);
  size_t copied = std::min(n, srcLen);
  if (isConcrete(src, copied) && isConcrete(dest, n))
    return result;

  auto srcShadow = ReadOnlyShadow(src, copied);
  auto destShadow = ReadWriteShadow(dest, n);

  std::copy(srcShadow.begin(), srcShadow.end(), destShadow.begin());
  if (copied < n) {
    ReadWriteShadow destRestShadow(dest + copied, n - copied);
    std::fill(destRestShadow.begin(), destRestShadow.end(), nullptr);
  }

  return result;
}

const char *SYM(strchr)(const char *s, int c) {
  tryAlternative(s, _sym_get_parameter_expression(0));
  tryAlternative(c, _sym_get_parameter_expression(1));

  auto result = strchr(s, c);
  _sym_set_return_expression(nullptr);

  Z3_ast cExpr = _sym_get_parameter_expression(1);
  if (isConcrete(s, result ? (result - s) : strlen(s)) && !cExpr)
    return result;

  if (!cExpr)
    cExpr = _sym_build_integer(c, 8);

  size_t length = result ? (result - s) : strlen(s);
  auto shadow = ReadOnlyShadow(s, length);
  auto shadowIt = shadow.begin();
  for (size_t i = 0; i < length; i++) {
    _sym_push_path_constraint(
        _sym_build_not_equal(
            *shadowIt ? *shadowIt : _sym_build_integer(s[i], 8), cExpr),
        true);
    ++shadowIt;
  }

  return result;
}

int SYM(memcmp)(const void *a, const void *b, size_t n) {
  tryAlternative(a, _sym_get_parameter_expression(0));
  tryAlternative(b, _sym_get_parameter_expression(1));
  tryAlternative(n, _sym_get_parameter_expression(2));

  auto result = memcmp(a, b, n);
  _sym_set_return_expression(nullptr);

  if (isConcrete(a, n) && isConcrete(b, n))
    return result;

  auto aShadowIt = ReadOnlyShadow(a, n).begin_non_null();
  auto bShadowIt = ReadOnlyShadow(b, n).begin_non_null();
  Z3_ast allEqual = _sym_build_equal(*aShadowIt, *bShadowIt);
  for (size_t i = 1; i < n; i++) {
    allEqual =
        _sym_build_and(allEqual, _sym_build_equal(*aShadowIt, *bShadowIt));
    ++aShadowIt;
    ++bShadowIt;
  }

  _sym_push_path_constraint(allEqual, result ? false : true);
  return result;
}
}

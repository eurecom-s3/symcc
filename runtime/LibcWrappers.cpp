#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "Config.h"
#include "Shadow.h"
#include <Runtime.h>

#define SYM(x) x##_symbolized

// #define DEBUG_RUNTIME
#ifdef DEBUG_RUNTIME
#include <iostream>
#endif

namespace {

/// The file descriptor referring to the symbolic input.
static int inputFileDescriptor = -1;

/// The current position in the (symbolic) input.
static size_t inputOffset = 0;

/// Tell the solver to try an alternative value than the given one.
template <typename V, typename F>
void tryAlternative(V value, SymExpr valueExpr, F caller) {
  if (valueExpr) {
    _sym_push_path_constraint(
        _sym_build_equal(valueExpr,
                         _sym_build_integer(value, sizeof(value) * 8)),
        true, reinterpret_cast<uintptr_t>(caller));
  }
}

// A partial specialization for pointer types for convenience.
template <typename E, typename F>
void tryAlternative(E *value, SymExpr valueExpr, F caller) {
  tryAlternative(reinterpret_cast<intptr_t>(value), valueExpr, caller);
}
} // namespace

void initLibcWrappers() {
  if (g_config.fullyConcrete)
    return;

  if (g_config.inputFile.empty()) {
    // Symbolic data comes from standard input.
    inputFileDescriptor = 0;
  }
}

extern "C" {

void *SYM(malloc)(size_t size) {
  auto result = malloc(size);

  tryAlternative(size, _sym_get_parameter_expression(0), SYM(malloc));

  _sym_set_return_expression(nullptr);
  return result;
}

void *SYM(calloc)(size_t nmemb, size_t size) {
  auto result = calloc(nmemb, size);

  tryAlternative(nmemb, _sym_get_parameter_expression(0), SYM(calloc));
  tryAlternative(size, _sym_get_parameter_expression(1), SYM(calloc));

  _sym_set_return_expression(nullptr);
  return result;
}

void *SYM(mmap)(void *addr, size_t len, int prot, int flags, int fildes,
                off_t off) {
  auto result = mmap(addr, len, prot, flags, fildes, off);

  tryAlternative(len, _sym_get_parameter_expression(1), SYM(mmap));

  _sym_set_return_expression(nullptr);
  return result;
}

int SYM(open)(const char *path, int oflag, mode_t mode) {
  auto result = open(path, oflag, mode);
  _sym_set_return_expression(nullptr);

  if (result >= 0 && strstr(path, g_config.inputFile.c_str()) != nullptr) {
    if (inputFileDescriptor != -1)
      std::cerr << "Warning: input file opened multiple times; this is not yet "
                   "supported"
                << std::endl;
    inputFileDescriptor = result;
  }

  return result;
}

ssize_t SYM(read)(int fildes, void *buf, size_t nbyte) {
  tryAlternative(buf, _sym_get_parameter_expression(1), SYM(read));
  tryAlternative(nbyte, _sym_get_parameter_expression(2), SYM(read));

  auto result = read(fildes, buf, nbyte);
  _sym_set_return_expression(nullptr);

  if (result < 0)
    return result;

  if (fildes == inputFileDescriptor) {
    // Reading symbolic input.
    ReadWriteShadow shadow(buf, nbyte);
    std::generate(shadow.begin(), shadow.end(),
                  []() { return _sym_get_input_byte(inputOffset++); });
  } else if (!isConcrete(buf, nbyte)) {
    ReadWriteShadow shadow(buf, nbyte);
    std::fill(shadow.begin(), shadow.end(), nullptr);
  }

  return result;
}

FILE *SYM(fopen)(const char *pathname, const char *mode) {
  auto result = fopen(pathname, mode);
  _sym_set_return_expression(nullptr);

  if (result != nullptr &&
      strstr(pathname, g_config.inputFile.c_str()) != nullptr) {
    if (inputFileDescriptor != -1)
      std::cerr << "Warning: input file opened multiple times; this is not yet "
                   "supported"
                << std::endl;
    inputFileDescriptor = fileno(result);
  }

  return result;
}

size_t SYM(fread)(void *ptr, size_t size, size_t nmemb, FILE *stream) {
  tryAlternative(ptr, _sym_get_parameter_expression(0), SYM(fread));
  tryAlternative(size, _sym_get_parameter_expression(1), SYM(fread));
  tryAlternative(nmemb, _sym_get_parameter_expression(2), SYM(fread));

  auto result = fread(ptr, size, nmemb, stream);
  _sym_set_return_expression(nullptr);

  if (fileno(stream) == inputFileDescriptor) {
    // Reading symbolic input.
    ReadWriteShadow shadow(ptr, result * size);
    std::generate(shadow.begin(), shadow.end(),
                  []() { return _sym_get_input_byte(inputOffset++); });
  } else if (!isConcrete(ptr, result * size)) {
    ReadWriteShadow shadow(ptr, result * size);
    std::fill(shadow.begin(), shadow.end(), nullptr);
  }

  return result;
}

int SYM(getc)(FILE *stream) {
  auto result = getc(stream);
  if (result == EOF) {
    _sym_set_return_expression(nullptr);
    return result;
  }

  if (fileno(stream) == inputFileDescriptor)
    _sym_set_return_expression(_sym_build_zext(
        _sym_get_input_byte(inputOffset++), sizeof(int) * 8 - 8));
  else
    _sym_set_return_expression(nullptr);

  return result;
}

int SYM(ungetc)(int c, FILE *stream) {
  auto result = ungetc(c, stream);
  _sym_set_return_expression(_sym_get_parameter_expression(0));

  if (fileno(stream) == inputFileDescriptor && result != EOF)
    inputOffset--;

  return result;
}

void *SYM(memcpy)(void *dest, const void *src, size_t n) {
  auto result = memcpy(dest, src, n);

  tryAlternative(dest, _sym_get_parameter_expression(0), SYM(memcpy));
  tryAlternative(src, _sym_get_parameter_expression(1), SYM(memcpy));
  tryAlternative(n, _sym_get_parameter_expression(2), SYM(memcpy));

  _sym_memcpy(static_cast<uint8_t *>(dest), static_cast<const uint8_t *>(src),
              n);
  _sym_set_return_expression(_sym_get_parameter_expression(0));
  return result;
}

void *SYM(memset)(void *s, int c, size_t n) {
  auto result = memset(s, c, n);

  tryAlternative(s, _sym_get_parameter_expression(0), SYM(memset));
  tryAlternative(n, _sym_get_parameter_expression(2), SYM(memset));

  _sym_memset(static_cast<uint8_t *>(s), _sym_get_parameter_expression(1), n);
  _sym_set_return_expression(_sym_get_parameter_expression(0));
  return result;
}

void *SYM(memmove)(void *dest, const void *src, size_t n) {
  tryAlternative(dest, _sym_get_parameter_expression(0), SYM(memmove));
  tryAlternative(src, _sym_get_parameter_expression(1), SYM(memmove));
  tryAlternative(n, _sym_get_parameter_expression(2), SYM(memmove));

  auto result = memmove(dest, src, n);
  _sym_memmove(static_cast<uint8_t *>(dest), static_cast<const uint8_t *>(src),
               n);

  _sym_set_return_expression(_sym_get_parameter_expression(0));
  return result;
}

char *SYM(strncpy)(char *dest, const char *src, size_t n) {
  tryAlternative(dest, _sym_get_parameter_expression(0), SYM(strncpy));
  tryAlternative(src, _sym_get_parameter_expression(1), SYM(strncpy));
  tryAlternative(n, _sym_get_parameter_expression(2), SYM(strncpy));

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
  tryAlternative(s, _sym_get_parameter_expression(0), SYM(strchr));
  tryAlternative(c, _sym_get_parameter_expression(1), SYM(strchr));

  auto result = strchr(s, c);
  _sym_set_return_expression(nullptr);

  auto cExpr = _sym_get_parameter_expression(1);
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
        true, reinterpret_cast<uintptr_t>(SYM(strchr)));
    ++shadowIt;
  }

  return result;
}

int SYM(memcmp)(const void *a, const void *b, size_t n) {
  tryAlternative(a, _sym_get_parameter_expression(0), SYM(memcmp));
  tryAlternative(b, _sym_get_parameter_expression(1), SYM(memcmp));
  tryAlternative(n, _sym_get_parameter_expression(2), SYM(memcmp));

  auto result = memcmp(a, b, n);
  _sym_set_return_expression(nullptr);

  if (isConcrete(a, n) && isConcrete(b, n))
    return result;

  auto aShadowIt = ReadOnlyShadow(a, n).begin_non_null();
  auto bShadowIt = ReadOnlyShadow(b, n).begin_non_null();
  auto allEqual = _sym_build_equal(*aShadowIt, *bShadowIt);
  for (size_t i = 1; i < n; i++) {
    ++aShadowIt;
    ++bShadowIt;
    allEqual =
        _sym_build_bool_and(allEqual, _sym_build_equal(*aShadowIt, *bShadowIt));
  }

  _sym_push_path_constraint(allEqual, result ? false : true,
                            reinterpret_cast<uintptr_t>(SYM(memcmp)));
  return result;
}
}

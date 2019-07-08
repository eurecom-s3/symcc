#include <iostream>

// Includes from compiler-rt
#include <interception/interception.h>

#include "Runtime.h"

// The macros from compiler-rt define publicly visible interceptors, so all code
// uses them once they are defined. We only want to intercept calls from some
// objects; therefore, we define hidden interceptors and link them statically
// into any such object.
#undef INTERCEPTOR_ATTRIBUTE
#define INTERCEPTOR_ATTRIBUTE __attribute__((visibility("hidden")))
#undef DECLARE_WRAPPER
#define DECLARE_WRAPPER(ret_type, func, ...)                                   \
  extern "C" ret_type func(__VA_ARGS__) __attribute__(                         \
      (weak, alias("__interceptor_" #func), visibility("hidden")));

//
// The interceptors
//

INTERCEPTOR(void *, mmap, void *addr, SIZE_T sz, int prot, int flags, int fd,
            OFF_T off) {
  auto result = REAL(mmap)(addr, sz, prot, flags, fd, off);
  _sym_set_return_expression(_sym_build_integer(
      reinterpret_cast<uint64_t>(result), sizeof(result) * 8));
  return result;
}

INTERCEPTOR(char *, getenv, const char *name) {
#ifdef DEBUG_RUNTIME
  std::cout << "Intercepting call to getenv with argument " << name
            << std::endl;
#endif
  auto result = REAL(getenv)(name);
  // TODO register string memory?
  _sym_set_return_expression(_sym_build_integer(
      reinterpret_cast<uint64_t>(result), sizeof(result) * 8));
  return result;
}

extern "C" void _initialize_interception() {
#ifdef DEBUG_RUNTIME
  std::cout << "Initializing libc interceptors" << std::endl;
#endif

  INTERCEPT_FUNCTION(mmap);
  INTERCEPT_FUNCTION(getenv);
}

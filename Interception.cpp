#include <iostream>

// Includes from compiler-rt
#include <interception/interception.h>

#include "Runtime.h"

INTERCEPTOR(void *, mmap, void *addr, SIZE_T sz, int prot, int flags, int fd,
            OFF_T off) {
  auto result = REAL(mmap)(addr, sz, prot, flags, fd, off);
  _sym_set_return_expression(_sym_build_integer(
      reinterpret_cast<uint64_t>(result), sizeof(result) * 8));
  return result;
}

void initialize_interception() {
#ifdef DEBUG_RUNTIME
  std::cout << "Initializing libc interceptors" << std::endl;
#endif

  INTERCEPT_FUNCTION(mmap);
}

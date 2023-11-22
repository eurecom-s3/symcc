
#include "ShadowTrace.h"
#include "Shadow.h"

void dumpSymbolicState() {
  for (auto const& [pageAddress, _] : g_shadow_pages) {
    for(auto byteAddress = pageAddress; byteAddress < pageAddress + kPageSize; byteAddress++) {
      auto byteExpr = _sym_read_memory((u_int8_t *) byteAddress, 1, true);
      if (byteExpr != nullptr && !byteExpr->isConcrete()) {
        printf("%lx has symbolic value\n", byteAddress);
      }
    }
  }
  printf("\n");
}
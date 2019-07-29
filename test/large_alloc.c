// RUN: %symcc -O2 %s -o %t
// RUN: %t | FileCheck %s
//
// Make sure that we can handle large allocations symbolically.
#include <stdio.h>
#include <stdlib.h>

int sym_make_symbolic(const char *, int, uint8_t);

int main(int argc, char *argv[]) {
  int x = sym_make_symbolic("x", 42, 32);

  char *largeAllocation = malloc(10000);
  memset(largeAllocation, (char)x, 10000);

  printf("%s\n", (largeAllocation[9999] == 42) ? "worked" : "error");
  // CHECK: Trying to solve
  // CHECK: Found diverging input

  return 0;
}

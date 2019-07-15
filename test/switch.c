// RUN: %symcc -O2 %s -o %t
// RUN: %t | FileCheck %s
//
// Check the symbolic handling of "read"

#include <stdio.h>
#include <stdint.h>

int sym_make_symbolic(const char*, int, uint8_t);

int main(int argc, char* argv[]) {
  int x = sym_make_symbolic("x", 5, 32);

  switch (x) {
  case 3:
    printf("x is 3\n");
    break;
  case 4:
    printf("x is 4\n");
    break;
  case 5:
    printf("x is 5\n");
    break;
  default:
    printf("x is something else\n");
    break;
  }
  // CHECK: Trying to solve
  // CHECK: Found diverging input
  // CHECK: x is 5

  // When the value to branch on is concrete there should be no solver
  // interaction.
  int y = 17;
  switch (y) {
  case 3:
    printf("y is 3\n");
    break;
  case 4:
    printf("y is 4\n");
    break;
  case 5:
    printf("y is 5\n");
    break;
  default:
    printf("y is something else\n");
    break;
  }
  // CHECK-NOT: Trying to solve
  // CHECK: y is something else

  return 0;
}

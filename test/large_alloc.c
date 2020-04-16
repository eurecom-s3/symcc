// RUN: %symcc %s -o %t
// RUN: echo -ne "\x2a\x00\x00\x00" | %t 2>&1 | %filecheck %s
// RUN: %symcc -m32 %s -o %t_32
// RUN: echo -ne "\x2a\x00\x00\x00" | %t_32 2>&1 | %filecheck %s
//
// Make sure that we can handle large allocations symbolically. Also, test
// memory-related library functions.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  int x;
  if (read(STDIN_FILENO, &x, sizeof(x)) != sizeof(x)) {
    printf("Failed to read x\n");
    return -1;
  }

  char *largeAllocation = malloc(10000);
  memset(largeAllocation, (char)x, 10000);

  printf("%s\n", (largeAllocation[9999] < 100) ? "worked" : "error");
  // SIMPLE: Trying to solve
  // SIMPLE: Found diverging input
  // QSYM-COUNT-2: SMT
  // QSYM: New testcase
  // ANY: worked

  memset(largeAllocation, 'A', 10000);
  printf("%s\n", (largeAllocation[5000] == 17) ? "true" : "false");
  // SIMPLE-NOT: Trying to solve
  // QSYM-NOT: SMT
  // ANY: false

  memset(largeAllocation, x, 10000);
  printf("%s\n", (largeAllocation[5000] > 100) ? "true" : "false");
  // SIMPLE: Trying to solve
  // SIMPLE: Can't find a diverging input at this point
  // QSYM-COUNT-2: SMT
  // (Qsym finds a new test case with the optimistic strategy.)
  // ANY: false

  memcpy(largeAllocation + x, &x, sizeof(x));
  // SIMPLE: Trying to solve
  // SIMPLE: Found diverging input
  // QSYM-COUNT-2: SMT
  // QSYM: New testcase

  memcpy(largeAllocation, &x, sizeof(x));
  // SIMPLE-NOT: Trying to solve
  // QSYM-NOT: SMT

  memmove(largeAllocation + 1, largeAllocation, sizeof(x));
  printf("%s\n", (largeAllocation[0] == largeAllocation[2]) ? "true" : "false");
  // SIMPLE: Trying to solve
  // QSYM-COUNT-2: SMT
  // TODO should find new inputs
  // ANY: false

  return 0;
}

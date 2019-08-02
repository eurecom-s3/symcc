// RUN: %symcc -O2 %s -o %t
// RUN: echo -ne "\x2a\x00\x00\x00" | %t 2>&1 | %filecheck %s
//
// Make sure that we can handle large allocations symbolically.
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

  printf("%s\n", (largeAllocation[9999] == 42) ? "worked" : "error");
  // SIMPLE: Trying to solve
  // SIMPLE: Found diverging input
  // QSYM-COUNT-2: SMT
  // QSYM: New testcase

  return 0;
}

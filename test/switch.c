// RUN: %symcc -O2 %s -o %t
// RUN: echo -ne "\x05\x00\x00\x00" | %t 2>&1 | %filecheck %s
//
// Check the symbolic handling of "read"

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
  int x;
  if (read(STDIN_FILENO, &x, sizeof(x)) != sizeof(x)) {
    printf("Failed to read x\n");
    return -1;
  }

  int foo = 0;
  switch (x) {
  case 3:
    foo = 0;
    printf("x is 3\n");
    break;
  case 4:
    foo = 1;
    // Deliberately not printing anything here, which will generate a direct
    // jump to the block after the switch statement.
    break;
  case 5:
    foo = 2;
    printf("x is 5\n");
    break;
  default:
    foo = 3;
    printf("x is something else\n");
    break;
  }
  // SIMPLE: Trying to solve
  // SIMPLE: Found diverging input
  // QSYM-COUNT-2: SMT
  // QSYM: New testcase
  // ANY: x is 5

  printf("%d\n", foo);
  // ANY: 2

  // When the value to branch on is concrete there should be no solver
  // interaction.
  volatile int y = 17;
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
  // SIMPLE-NOT: Trying to solve
  // QSYM-NOT: SMT
  // ANY: y is something else

  return 0;
}

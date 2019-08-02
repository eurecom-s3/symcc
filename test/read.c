// RUN: %symcc -O2 %s -o %t
// RUN: echo b | %t 2>&1 | %filecheck %s
//
// Check the symbolic handling of "read"

#include <stdio.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
  char c;

  ssize_t nbytes = read(STDIN_FILENO, &c, 1);
  if (nbytes != 1)
    return 1;

  // SIMPLE: Trying to solve
  // SIMPLE: Found diverging input
  // SIMPLE: stdin0 -> #x61
  // QSYM-COUNT-2: SMT
  // QSYM: New testcase
  if (c == 'a')
    printf("Correct\n");
  else
    printf("Next time...\n");
  // ANY: Next time...
  return 0;
}

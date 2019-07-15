// RUN: %symcc -O2 %s -o %t
// RUN: echo b | %t | FileCheck %s
//
// Check the symbolic handling of "read"

#include <stdio.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
  char c;

  ssize_t nbytes = read(STDIN_FILENO, &c, 1);
  if (nbytes != 1)
    return 1;

  // CHECK: Trying to solve
  // CHECK: Found diverging input
  // CHECK: stdin0 -> #x61
  if (c == 'a')
    printf("Correct\n");
  else
    printf("Next time...\n");
  // CHECK: Next time...
  return 0;
}

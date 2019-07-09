// RUN: %symcc -O2 %s -o %t
// RUN: echo b | %t | FileCheck %s
//
// Check the symbolic handling of "read"

// TODO make sure that input is treated as symbolic

#include <stdio.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
  char c;
  read(STDIN_FILENO, &c, 1);
  if (c == 'a')
    printf("Correct\n");
  else
    printf("Next time...\n");
  // CHECK: Next time...
  return 0;
}

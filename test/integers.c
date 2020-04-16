// RUN: %symcc -O2 %s -o %t
// RUN: echo -ne "\x05\x00\x00\x00\x00\x00\x00\x00" | %t 2>&1 | %filecheck %s
// RUN: %symcc -m32 -O2 %s -o %t_32
// RUN: echo -ne "\x05\x00\x00\x00\x00\x00\x00\x00" | %t_32 2>&1 | %filecheck %s
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

uint64_t g_value = 0xaaaabbbbccccdddd;

int main(int argc, char *argv[]) {
  uint64_t x;
  if (read(STDIN_FILENO, &x, sizeof(x)) != sizeof(x)) {
    printf("Failed to read x\n");
    return -1;
  }

  printf("%s\n", (x == g_value) ? "yes" : "no");
  // SIMPLE: Trying to solve
  // SIMPLE: Found diverging input
  // Make sure that we don't truncate integers.
  // SIMPLE-DAG: #xaa
  // SIMPLE-DAG: #xbb
  // SIMPLE-DAG: #xcc
  // SIMPLE-DAG: #xdd
  // QSYM-COUNT-2: SMT
  // ANY: no

  return 0;
}

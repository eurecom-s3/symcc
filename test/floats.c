// RUN: %symcc -O2 %s -o %t
// RUN: echo -ne "\x05\x00\x00\x00" | %t 2>&1 | %filecheck %s
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

float g_value = 0.1234;

int main(int argc, char *argv[]) {
  int x;
  if (read(STDIN_FILENO, &x, sizeof(x)) != sizeof(x)) {
    printf("Failed to read x\n");
    return -1;
  }

  g_value += x;
  printf("%f\n", g_value);
  // ANY: 5.1234

  printf("%s\n", ((g_value < 7) && (g_value > 6)) ? "yes" : "no");
  // SIMPLE: Trying to solve
  // SIMPLE: Found diverging input
  // SIMPLE: #x06
  // Qsym doesn't support symbolic floats!
  // QSYM-NOT: SMT
  // ANY: no

  return 0;
}

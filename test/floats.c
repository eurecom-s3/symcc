// RUN: %symcc -O2 %s -o %t
// RUN: echo -ne "\x05\x00\x00\x00" | %t | FileCheck %s
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
  // CHECK: 5.1234

  printf("%s\n", ((g_value < 7) && (g_value > 6)) ? "yes" : "no");
  // CHECK: Trying to solve
  // CHECK: Found diverging input
  // CHECK: #x06
  // CHECK: no

  return 0;
}

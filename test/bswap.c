// RUN: %symcc %s -o %t
// RUN: echo -ne "\x01\x02\x03\x04" | %t 2>&1 | %filecheck %s
// RUN: %symcc -m32 %s -o %t_32
// RUN: echo -ne "\x01\x02\x03\x04" | %t_32 2>&1 | %filecheck %s
// RUN: %symcc %s -S -emit-llvm -o - | FileCheck --check-prefix=BITCODE %s
// RUN: %symcc %s -m32 -S -emit-llvm -o - | FileCheck --check-prefix=BITCODE %s
//
// Here we test that the "bswap" intrinsic is handled correctly.
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
  uint32_t x;
  if (read(STDIN_FILENO, &x, sizeof(x)) != sizeof(x)) {
    printf("Failed to read x\n");
    return -1;
  }

  // BITCODE: llvm.bswap.i32
  uint32_t y = __builtin_bswap32(x);

  // ANY: 0x04030201 0x01020304
  printf("0x%08x 0x%08x\n", x, y);

  // SIMPLE: Trying to solve
  // SIMPLE: Found diverging input
  // SIMPLE-DAG: stdin0 -> #xca
  // SIMPLE-DAG: stdin1 -> #xfe
  // SIMPLE-DAG: stdin2 -> #xbe
  // SIMPLE-DAG: stdin3 -> #xef
  // QSYM-COUNT-2: SMT
  // ANY: Not quite.
  if (y == 0xcafebeef)
    printf("Correct test input.\n");
  else
    printf("Not quite.\n");

  return 0;
}

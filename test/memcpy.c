// RUN: %symcc -O2 %s -o %t
// RUN: %t | FileCheck %s
//
// Test that we generate alternative inputs for the parameters to memcpy (which
// should assert that the concept works for other functions as well).
#include <stdio.h>
#include <stdint.h>
#include <string.h>

int sym_make_symbolic(const char*, int, uint8_t);

int main(int argc, char* argv[]) {
  char values[] = {1, 2, 3};
  char values_copy[3];

  int dest_offset = sym_make_symbolic("dest_offset", 0, 32);
  int src_offset = sym_make_symbolic("src_offset", 0, 32);
  int length = sym_make_symbolic("length", 3, 32);

  memcpy(values_copy + dest_offset, values + src_offset, length);
  printf("%d\n", values_copy[0]);
  // CHECK: Trying to solve
  // CHECK: Found diverging input
  // CHECK: dest_offset
  // CHECK: Trying to solve
  // CHECK: Found diverging input
  // CHECK-DAG: dest_offset -> #x{{0+$}}
  // CHECK-DAG: src_offset
  // CHECK: Trying to solve
  // CHECK: Found diverging input
  // CHECK-DAG: dest_offset -> #x{{0+$}}
  // CHECK-DAG: src_offset -> #x{{0+$}}
  // CHECK-DAG: length

  return 0;
}

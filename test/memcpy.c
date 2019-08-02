// RUN: %symcc -O2 %s -o %t
// RUN: echo -ne "\x00\x00\x00\x00\x00\x00\x00\x00\x03\x00\x00\x00" | %t 2>&1 | %filecheck %s
//
// Test that we generate alternative inputs for the parameters to memcpy (which
// should assert that the concept works for other functions as well).
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  char values[] = {1, 2, 3};
  char values_copy[3];

  int dest_offset;
  if (read(STDIN_FILENO, &dest_offset, sizeof(dest_offset)) !=
      sizeof(dest_offset)) {
    printf("Failed to read dest_offset\n");
    return -1;
  }
  int src_offset;
  if (read(STDIN_FILENO, &src_offset, sizeof(src_offset)) !=
      sizeof(src_offset)) {
    printf("Failed to read src_offset\n");
    return -1;
  }
  int length;
  if (read(STDIN_FILENO, &length, sizeof(length)) != sizeof(length)) {
    printf("Failed to read length\n");
    return -1;
  }

  memcpy(values_copy + dest_offset, values + src_offset, length);
  printf("%d\n", values_copy[0]);
  // SIMPLE: Trying to solve
  // SIMPLE: Found diverging input
  // SIMPLE: stdin3
  // QSYM-COUNT-2: SMT
  // QSYM: New testcase
  // SIMPLE: Trying to solve
  // SIMPLE: Found diverging input
  // SIMPLE-DAG: stdin3 -> #x00
  // SIMPLE-DAG: stdin7
  // QSYM-COUNT-2: SMT
  // QSYM: New testcase
  // SIMPLE: Trying to solve
  // SIMPLE: Found diverging input
  // SIMPLE-DAG: stdin3 -> #x00
  // SIMPLE-DAG: stdin7 -> #x00
  // SIMPLE-DAG: stdin11
  // QSYM-COUNT-2: SMT
  // QSYM: New testcase

  return 0;
}

// This file is part of SymCC.
//
// SymCC is free software: you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// SymCC is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with
// SymCC. If not, see <https://www.gnu.org/licenses/>.

// RUN: %symcc -O2 %s -o %t
// RUN: echo -ne "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x03" | %t 2>&1 | %filecheck %s
//
// Test that we generate alternative inputs for the parameters to memcpy (which
// should assert that the concept works for other functions as well). Also, make
// sure that we handle the different parameter sizes for mmap correctly.

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <arpa/inet.h>
#include <sys/mman.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  char values[] = {1, 2, 3};
  char values_copy[3];

  int dest_offset;
  if (read(STDIN_FILENO, &dest_offset, sizeof(dest_offset)) !=
      sizeof(dest_offset)) {
    fprintf(stderr, "Failed to read dest_offset\n");
    return -1;
  }
  dest_offset = ntohl(dest_offset);
  int src_offset;
  if (read(STDIN_FILENO, &src_offset, sizeof(src_offset)) !=
      sizeof(src_offset)) {
    fprintf(stderr, "Failed to read src_offset\n");
    return -1;
  }
  src_offset = ntohl(src_offset);
  int length;
  if (read(STDIN_FILENO, &length, sizeof(length)) != sizeof(length)) {
    fprintf(stderr, "Failed to read length\n");
    return -1;
  }
  length = ntohl(length);

  memcpy(values_copy + dest_offset, values + src_offset, length);
  fprintf(stderr, "%d\n", values_copy[0]);
  // SIMPLE: Trying to solve
  // SIMPLE: Found diverging input
  // SIMPLE: stdin{{[0-3]}}
  // QSYM-COUNT-2: SMT
  // QSYM: New testcase
  // SIMPLE: Trying to solve
  // SIMPLE: Found diverging input
  // SIMPLE-DAG: stdin{{[0-3]}} -> #x00
  // SIMPLE-DAG: stdin{{[4-7]}} -> #x{{.?[^0].?}}
  // QSYM-COUNT-2: SMT
  // QSYM: New testcase
  // SIMPLE: Trying to solve
  // SIMPLE: Found diverging input
  // SIMPLE-DAG: stdin{{[0-7]}} -> #x00
  // QSYM-COUNT-2: SMT
  // QSYM: New testcase
  // ANY: 1

  void *pointer = mmap(NULL, 8, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  puts(pointer == MAP_FAILED ? "failed" : "succeeded");
  // ANY: succeeded

  return 0;
}

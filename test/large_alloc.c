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

// RUN: %symcc %s -o %t
// RUN: echo -ne "\x00\x00\x00\x2a" | %t 2>&1 | %filecheck %s
//
// Make sure that we can handle large allocations symbolically. Also, test
// memory-related library functions.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  int x;
  if (read(STDIN_FILENO, &x, sizeof(x)) != sizeof(x)) {
    fprintf(stderr, "Failed to read x\n");
    return -1;
  }
  int netlongX = x;
  x = ntohl(x);

  char *largeAllocation = malloc(10000);
  memset(largeAllocation, (char)x, 10000);

  fprintf(stderr, "%s\n", (largeAllocation[9999] < 100) ? "worked" : "error");
  // SIMPLE: Trying to solve
  // SIMPLE: Found diverging input
  // QSYM-COUNT-2: SMT
  // QSYM: New testcase
  // ANY: worked

  memset(largeAllocation, 'A', 10000);
  fprintf(stderr, "%s\n", (largeAllocation[5000] == 17) ? "true" : "false");
  // SIMPLE-NOT: Trying to solve
  // QSYM-NOT: SMT
  // ANY: false

  memset(largeAllocation, x, 10000);
  fprintf(stderr, "%s\n", (largeAllocation[5000] > 100) ? "true" : "false");
  // SIMPLE: Trying to solve
  // SIMPLE: Can't find a diverging input at this point
  // QSYM-COUNT-2: SMT
  // (Qsym finds a new test case with the optimistic strategy.)
  // ANY: false

  memcpy(largeAllocation + x, &x, sizeof(x));
  // SIMPLE: Trying to solve
  // SIMPLE: Found diverging input
  // QSYM-COUNT-2: SMT
  // QSYM: New testcase

  // Make x little-endian.
  x = __builtin_bswap32(netlongX);

  memcpy(largeAllocation, &x, sizeof(x));
  // SIMPLE-NOT: Trying to solve
  // QSYM-NOT: SMT

  memmove(largeAllocation + 1, largeAllocation, sizeof(x));
  fprintf(stderr, "%s\n", (largeAllocation[0] == largeAllocation[2]) ? "true" : "false");
  // SIMPLE: Trying to solve
  // QSYM-COUNT-2: SMT
  // TODO should find new inputs
  // ANY: false

  return 0;
}

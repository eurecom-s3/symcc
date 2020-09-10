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
// RUN: echo -ne "\x00\x00\x00\x05" | %t 2>&1 | %filecheck %s
//
// Check the symbolic handling of "read"

#include <stdint.h>
#include <stdio.h>

#include <arpa/inet.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
  int x;
  if (read(STDIN_FILENO, &x, sizeof(x)) != sizeof(x)) {
    fprintf(stderr, "Failed to read x\n");
    return -1;
  }
  x = ntohl(x);

  int foo = 0;
  switch (x) {
  case 3:
    foo = 0;
    fprintf(stderr, "x is 3\n");
    break;
  case 4:
    foo = 1;
    // Deliberately not printing anything here, which will generate a direct
    // jump to the block after the switch statement.
    break;
  case 5:
    foo = 2;
    fprintf(stderr, "x is 5\n");
    break;
  default:
    foo = 3;
    fprintf(stderr, "x is something else\n");
    break;
  }
  // SIMPLE: Trying to solve
  // SIMPLE: Found diverging input
  // QSYM-COUNT-2: SMT
  // QSYM: New testcase
  // ANY: x is 5

  fprintf(stderr, "%d\n", foo);
  // ANY: 2

  // When the value to branch on is concrete there should be no solver
  // interaction.
  volatile int y = 17;
  switch (y) {
  case 3:
    fprintf(stderr, "y is 3\n");
    break;
  case 4:
    fprintf(stderr, "y is 4\n");
    break;
  case 5:
    fprintf(stderr, "y is 5\n");
    break;
  default:
    fprintf(stderr, "y is something else\n");
    break;
  }
  // SIMPLE-NOT: Trying to solve
  // QSYM-NOT: SMT
  // ANY: y is something else

  return 0;
}

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
// RUN: echo -ne "\x05\x00\x00\x00\x00\x00\x00\x00" | %t 2>&1 | %filecheck %s
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

uint64_t g_value = 0xaaaabbbbccccdddd;

int main(int argc, char *argv[]) {
  uint64_t x;
  if (read(STDIN_FILENO, &x, sizeof(x)) != sizeof(x)) {
    fprintf(stderr, "Failed to read x\n");
    return -1;
  }

  fprintf(stderr, "%s\n", (x == g_value) ? "yes" : "no");
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

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
// RUN: env SYMCC_MEMORY_INPUT=1 %t 2>&1 | %filecheck %s
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

void symcc_make_symbolic(const void *start, size_t byte_length);

uint64_t g_value = 0xaaaabbbbccccdddd;

int main(int argc, char *argv[]) {
  uint64_t x = 10;
  uint8_t y = 0;

  symcc_make_symbolic(&x, sizeof(x));
  symcc_make_symbolic(&y, sizeof(y));

  fprintf(stderr, "%s\n", (x == g_value) ? "yes" : "no");
  // SIMPLE: Trying to solve
  // SIMPLE: Found diverging input
  // SIMPLE-DAG: #xaa
  // SIMPLE-DAG: #xbb
  // SIMPLE-DAG: #xcc
  // SIMPLE-DAG: #xdd
  // QSYM-COUNT-2: SMT
  // ANY: no

  fprintf(stderr, "%s\n", (y == 10) ? "yes" : "no");
  // SIMPLE: Trying to solve
  // SIMPLE: Found diverging input
  // y should be part of the input, just after x
  // SIMPLE: stdin8 -> #x0a
  // QSYM-COUNT-2: SMT
  // ANY: no

  return 0;
}

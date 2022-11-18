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

// RUN: %symcc -O1 %s -o %t
// RUN: echo xxx | %t 2>&1 | %filecheck %s
//
// Check that select instruction is propagating the symbolic value (issue #109)

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

char bar(char a, char b, char c) { return (a == 0xA) ? b : c; }

int main() {
  char input[3] = {0};
  read(0, &input, sizeof(input));
  // SIMPLE: Trying to solve
  // SIMPLE: Found diverging input
  // SIMPLE: stdin0 -> #x0a
  // QSYM-COUNT-2: SMT
  // QSYM: New testcase
  char r = bar(input[0], input[1], input[2]);
  // SIMPLE: Trying to solve
  // SIMPLE: Found diverging input
  // SIMPLE-DAG: stdin2 -> #x0b
  // SIMPLE-DAG: stdin0 -> #x00
  // QSYM-COUNT-2: SMT
  // QSYM: New testcase
  // ANY: KO
  if (r == 0xB)
    printf("OK!\n");
  else
    printf("KO\n");
  return 0;
}

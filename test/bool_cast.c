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
// RUN: echo b | %t 2>&1 | %filecheck %s
//
// Check that bool cast is handled correctly (Issue #108)

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

int bar(unsigned char a) {
  if (a == 0xCA) return -1;
  else return 0;
}

int main() {
  unsigned char input = 0;
  read(0, &input, sizeof(input));
  int r = bar(input);
  // SIMPLE: Trying to solve
  // SIMPLE: Found diverging input
  // SIMPLE: stdin0 -> #xca
  // QSYM-COUNT-2: SMT
  // QSYM: New testcase
  if (r == -1) printf("Bingo!\n");
  else printf("Ok\n");
  // ANY: Ok
  return r;
}

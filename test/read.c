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
// RUN: echo b | %t 2>&1 | %filecheck %s
//
// Check the symbolic handling of "read"

#include <stdio.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
  char c;

  ssize_t nbytes = read(STDIN_FILENO, &c, 1);
  if (nbytes != 1)
    return 1;

  // SIMPLE: Trying to solve
  // SIMPLE: Found diverging input
  // SIMPLE: stdin0 -> #x61
  // QSYM-COUNT-2: SMT
  // QSYM: New testcase
  if (c == 'a')
    fprintf(stderr, "Correct\n");
  else
    fprintf(stderr, "Next time...\n");
  // ANY: Next time...
  return 0;
}

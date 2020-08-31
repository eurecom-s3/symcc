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
// RUN: echo -n test | %t 2>&1 | %filecheck %s
//
// Test the symbolic versions of string functions.

#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  char buffer[5];

  if (read(STDIN_FILENO, buffer, sizeof(buffer) - 1) !=
      sizeof(buffer) - 1) {
    fprintf(stderr, "Failed to read the input\n");
    return -1;
  }

  buffer[4] = '\0';

  // Fully concrete
  fputs(strchr("foobar", 'o') != NULL ? "found" : "nope", stderr);
  // SIMPLE-NOT: Trying to solve
  // QSYM-NOT: SMT
  // ANY: found

  // Symbolic buffer, concrete char
  fputs(strchr(buffer, 'x') != NULL ? "found" : "nope", stderr);
  // SIMPLE-COUNT-4: Found diverging input
  // QSYM: SMT
  // ANY: nope

  // Concrete buffer, symbolic char
  fputs(strchr("test", buffer[0]) != NULL ? "found" : "nope", stderr);
  // SIMPLE: Trying to solve
  //
  // QSYM's back-off mechanism kicks in because we're generating too many
  // queries; let's not check them anymore.
  //
  // ANY: found

  // Symbolic buffer, symbolic char
  fputs(strchr(buffer, buffer[1]) != NULL ? "found" : "nope", stderr);
  // SIMPLE-COUNT-2: Trying to solve
  // ANY: found

  return 0;
}

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

// Regression test for issue #83: SymCC failed to generate diverging inputs when
// a branch condition is produced through a logical negation. Clang lowers
// "!(0 == x)" to "xor i1 %cond, true", and the i1 result was passed to the
// runtime's _sym_push_path_constraint() as the "taken" argument with
// uninitialized high bits (observed as 254/255), so a not-taken branch was
// mis-recorded as taken and the solver explored the wrong direction.
//
// RUN: %symcc -O2 %s -o %t
// RUN: echo -ne "\x00\x00" | %t 2>&1 | %filecheck %s
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

int main(void) {
  uint16_t x = 0;
  if (read(STDIN_FILENO, &x, sizeof(x)) != sizeof(x)) {
    fprintf(stderr, "Failed to read input\n");
    return -1;
  }

  // "!(0 == x)" is lowered to an xor-based negation feeding the branch.
  // With the seed x == 0 the branch is NOT taken, so a correct SymCC must
  // push the path constraint and solve it for the other direction (x != 0).
  // SIMPLE: Trying to solve
  // QSYM: SMT
  int taken = !(0 == x);
  if (taken)
    fprintf(stderr, "taken\n");
  else
    fprintf(stderr, "not taken\n");
  // ANY: not taken
  return 0;
}

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
//
// Test symbolic handling of bcopy, bcmp, and bzero. We copy symbolic data with
// bcmp, then compare it with bcmp, expecting the solver to be triggered
// (indicating that the two functions are represented correctly); then we bzero
// the region and perform another comparison, which should not result in a
// solver query (indicating that bzero concretized as expected).

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <strings.h>

void symcc_make_symbolic(const void *start, size_t byte_length);
typedef void (*TestCaseHandler)(const void *, size_t);
void symcc_set_test_case_handler(TestCaseHandler handler);

int solved = 0;

void handle_test_case(const void *data, size_t data_length) {
  assert(data_length == 4);
  assert(bcmp(data, "bar", 4) == 0);
  solved = 1;
}

int main(int argc, char *argv[]) {
  symcc_set_test_case_handler(handle_test_case);

  const char input[] = "foo";
  symcc_make_symbolic(input, 4);

  // Make a copy and compare it in order to trigger the solver.
  char copy[4];
  bcopy(input, copy, 4);
  int bcmp_result = bcmp(copy, "bar", 4);
  assert(bcmp_result != 0);

  // Zero out the symbolic data and compare again (which should not trigger the
  // solver this time).
  bzero(copy, 4);
  bcmp_result = bcmp(copy, "abc", 4);
  assert(bcmp_result != 0);

  // The simple backend doesn't support test-case handlers, so we only expect a
  // solution with the QSYM backend.
  printf("Solved: %d\n", solved);
  // SIMPLE: Solved: 0
  // QSYM: Solved: 1
  return 0;
}

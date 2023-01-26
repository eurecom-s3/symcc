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

#define MAGIC 0xab

void symcc_make_symbolic(const void *start, size_t byte_length);
typedef void (*TestCaseHandler)(const void *, size_t);
void symcc_set_test_case_handler(TestCaseHandler handler);

int solved = 0;
int num_test_cases = 0;

void handle_test_case(const void *data, size_t data_length) {
  num_test_cases++;
  if (data_length == 1 && ((const uint8_t *)data)[0] == MAGIC)
    solved = 1;
}

int main(int argc, char *argv[]) {
  symcc_set_test_case_handler(handle_test_case);
  // SIMPLE: Warning: test-case handlers

  uint8_t input = 0;
  symcc_make_symbolic(&input, sizeof(input));

  fprintf(stderr, "%s\n", (input == MAGIC) ? "yes" : "no");
  // SIMPLE: Trying to solve
  // SIMPLE: Found diverging input
  // SIMPLE: stdin0 -> #xab
  // QSYM: SMT
  // ANY: no

  fprintf(stderr, "%d\n", solved);
  // QSYM: 1
  // SIMPLE: 0

  fprintf(stderr, "%d\n", num_test_cases);
  // QSYM: 1
  // SIMPLE: 0

  return 0;
}

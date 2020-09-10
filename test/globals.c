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
// Test that global variables are handled correctly. The special challenge is
// that we need to initialize the symbolic expression corresponding to any
// global variable that has an initial value.

#include <stdint.h>
#include <stdio.h>

#include <arpa/inet.h>
#include <unistd.h>

int g_increment = 17;
int g_uninitialized;
int g_more_than_one_byte_int = 512;

char g_values[] = {1, 2, 3};
int g_non_char_values[] = {300, 400, 500};

int increment(int x) {
    int result = x + g_increment;
    if (result < 30)
        return result;
    else
        return 42;
}

void sum(int x) {
    int result = 0;
    for (size_t i = 0; i < (sizeof(g_values) / sizeof(g_values[0])); i++) {
        result += g_values[i];
    }

    fprintf(stderr, "%s\n", (result < x) ? "foo" : "bar");
}

void sum_ints(int x) {
    int result = 0;
    for (size_t i = 0; i < (sizeof(g_non_char_values) / sizeof(g_non_char_values[0])); i++) {
        result += g_non_char_values[i];
    }

    fprintf(stderr, "%s\n", (result < x) ? "foo" : "bar");
}

int main(int argc, char* argv[]) {
    int x;
    if (read(STDIN_FILENO, &x, sizeof(x)) != sizeof(x)) {
        fprintf(stderr, "Failed to read x\n");
        return -1;
    }
    x = ntohl(x);

    fprintf(stderr, "%d\n", increment(x));
    // SIMPLE: Trying to solve
    // SIMPLE: (bvadd #x{{0*}}11
    // SIMPLE: Found diverging input
    // QSYM-COUNT-2: SMT
    // QSYM: New testcase
    // ANY: 22

    g_increment = 18;
    fprintf(stderr, "%d\n", increment(x));
    // SIMPLE: Trying to solve
    // SIMPLE: Found diverging input
    // We can't check for 0x12 here because with some versions of clang we end
    // up in a situation where (x + 18) >= 30 is folded into x >= 12.
    // QSYM-COUNT-2: SMT
    // QSYM: New testcase
    // ANY: 23

    g_uninitialized = 101;
    fprintf(stderr, "%s\n", (x < g_uninitialized) ? "smaller" : "greater or equal");
    // SIMPLE: Trying to solve
    // SIMPLE: (bvsle #x{{0*}}65
    // QSYM-COUNT-2: SMT
    // QSYM: New testcase
    // ANY: smaller

    sum(x);
    // SIMPLE: Trying to solve
    // SIMPLE-NOT: Can't find
    // SIMPLE: Found diverging input
    // QSYM-COUNT-2: SMT
    // QSYM: New testcase
    // ANY: bar

    fprintf(stderr, "%s\n", (x < g_more_than_one_byte_int) ? "true" : "false");
    // SIMPLE: Trying to solve
    // SIMPLE: #x{{0*}}200
    // SIMPLE: Can't find
    // QSYM-COUNT-2: SMT
    // ANY: true

    sum_ints(x);
    // SIMPLE: Trying to solve
    // SIMPLE: #x{{0*}}4b0
    // SIMPLE: Can't find
    // QSYM-COUNT-2: SMT
    // ANY: bar

    return 0;
}

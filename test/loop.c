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
// Make sure that our instrumentation works with back-jumps. Also, test support
// for 128-bit integers (if available).

#include <stdint.h>
#include <stdio.h>

#include <arpa/inet.h>
#include <unistd.h>

#ifdef __SIZEOF_INT128__
#define MYINT __int128
#else
#define MYINT int64_t
#endif

int fac(int x) {
    MYINT result = 1;

    // SIMPLE-COUNT-5: Found diverging input
    // SIMPLE-NOT: Found diverging input
    // QSYM-COUNT-5: New testcase
    for (MYINT i = 2; i <= x; i++)
        result *= i;

    return result;
}

int main(int argc, char* argv[]) {
    int x;
    if (read(STDIN_FILENO, &x, sizeof(x)) != sizeof(x)) {
        fprintf(stderr, "Failed to read x\n");
        return -1;
    }
    x = ntohl(x);
    fprintf(stderr, "%d\n", fac(x));
    return 0;
}

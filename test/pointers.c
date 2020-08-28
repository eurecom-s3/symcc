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
// RUN: echo -ne "\x05\x00\x00\x00\x12\x34\x56\x78\x90\xab\xcd\xef" | %t 2>&1 | %filecheck %s
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

volatile int g_value = 0x00ab0012;

int main(int argc, char* argv[]) {
    int x;
    void *ptr;
    setbuf(stdout, NULL);
    if (read(STDIN_FILENO, &x, sizeof(x)) != sizeof(x)) {
        printf("Failed to read x\n");
        return -1;
    }
    if (read(STDIN_FILENO, &ptr, sizeof(ptr)) != sizeof(ptr)) {
        printf("Failed to read ptr\n");
        return -1;
    }
    uint8_t *charPtr = (uint8_t*)&g_value;

    charPtr += 2;
    printf("%x\n", *charPtr);
    // ANY: ab

    printf("%s\n", (*charPtr == x) ? "equal" : "different");
    // SIMPLE: Trying to solve
    // SIMPLE: Found diverging input
    // SIMPLE: #xab
    // QSYM-COUNT-2: SMT
    // QSYM: New testcase
    // ANY: different

    volatile int local = 0x12345678;
    charPtr = (uint8_t*)&local;
    charPtr++;
    printf("%s\n", (*charPtr == x) ? "equal" : "different");
    // SIMPLE: Trying to solve
    // SIMPLE: Found diverging input
    // SIMPLE: #x56
    // QSYM-COUNT-2: SMT
    // QSYM: New testcase
    // ANY: different

    printf("%s\n", !ptr ? "null" : "not null");
    // SIMPLE: Trying to solve
    // SIMPLE: Found diverging input
    //
    // We expect a null pointer, but since pointer length varies between 32 and
    // 64-bit architectures we can't just expect N times #x00. Instead, we use a
    // regular expression that disallows nonzero values for anything but stdin0
    // (which is part of x, not ptr).
    //
    // SIMPLE-NOT: stdin{{[^0][0-9]?}} -> #x{{.?[^0].?}}
    // QSYM-COUNT-2: SMT
    // QSYM: New testcase
    // ANY: not null

    return 0;
}

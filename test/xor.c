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

// RUN: %symcc %s -o %t
// RUN: echo -ne "\x00\x00\x00" | %t 2>&1 | %filecheck %s
//
// Check the case that xor exist before push path constraint

#include <unistd.h>
#include <stdint.h>

struct S0 {
    uint16_t f0;           
};             

static int8_t g_2 = 0;
static uint16_t g_927 = 0;

static int32_t  func_1() {
    int32_t l_968 = 6;
    if (l_968 = !(0 == g_927)) {
        struct S0 l_974, l_1047;
        if (9 && (l_974.f0 = g_2 > 0 && l_1047.f0) == l_974.f0 <= 1);
    }
}
void main () {
    read(STDIN_FILENO, &g_2, sizeof(g_2));
    read(STDIN_FILENO, &g_927, sizeof(g_927));
    func_1();
    // SIMPLE: Trying to solve
    // SIMPLE: Found diverging input
    // QSYM-COUNT-2: SMT
    // QSYM: New testcase
}

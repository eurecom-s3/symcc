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
// RUN: echo -ne "\x05\x00\x00\x00" | %t 2>&1 | %filecheck %s
// This test is disabled until we can move the pass behind the optimizer in the pipeline:
// RUN-disabled: %symcc -O2 -emit-llvm -S %s -o - | FileCheck --check-prefix=BITCODE %s
//
// Here we test two things:
// 1. We can compile the file, and executing it symbolically results in solving
//    path constraints.
// 2. The bitcode is optimized, i.e., the instrumentation we insert does not
//    break compiler optimizations.
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

int foo(int a, int b) {
    // BITCODE-NOT: alloca
    // BITCODE-NOT: load
    // BITCODE-NOT: store
    // SIMPLE: Trying to solve
    // QSYM-COUNT-2: SMT
    // BITCODE: shl
    if (2 * a < b)
        return a;
    // SIMPLE: Trying to solve
    // QSYM-COUNT-2: SMT
    else if (a % b)
        return b;
    else
        return a + b;
}

int main(int argc, char* argv[]) {
    int x;
    if (read(STDIN_FILENO, &x, sizeof(x)) != sizeof(x)) {
        fprintf(stderr, "Failed to read x\n");
        return -1;
    }
    fprintf(stderr, "%d\n", x);
    fprintf(stderr, "%d\n", foo(x, 7));
    return 0;
}

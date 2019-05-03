// RUN: %symcc -O2 %s %symruntime -o %t
// RUN: %t | FileCheck %s
// RUN: %symcc -O2 -emit-llvm -S %s -o - | FileCheck --check-prefix=BITCODE %s
//
// Here we test two things:
// 1. We can compile the file, and executing it symbolically results in solving
//    path constraints.
// 2. The bitcode is optimized, i.e., the instrumentation we insert does not
//    break compiler optimizations.
#include <stdio.h>
#include <stdint.h>

int _sym_build_variable(const char*, int, uint8_t);

int foo(int a, int b) {
    // BITCODE-NOT: alloca
    // BITCODE-NOT: load
    // BITCODE-NOT: store
    // CHECK: Trying to solve
    // BITCODE: shl nsw i32 %0, 1
    if (2 * a < b)
        return a;
    // CHECK: Trying to solve
    else if (a % b)
        return b;
    else
        return a + b;
}

int main(int argc, char* argv[]) {
    int x = _sym_build_variable("x", 5, 32);
    printf("%d\n", foo(x, 7));
    return 0;
}

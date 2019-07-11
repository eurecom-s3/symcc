// RUN: %symcc -O2 %s -o %t
// RUN: %t | FileCheck %s
//
// Make sure that our instrumentation works with back-jumps.
#include <stdio.h>
#include <stdint.h>

int sym_make_symbolic(const char*, int, uint8_t);

int fac(int x) {
    int result = 1;

    // CHECK-COUNT-5: Found diverging input
    // CHECK-NOT: Found diverging input
    for (int i = 2; i <= x; i++)
        result *= i;

    return result;
}

int main(int argc, char* argv[]) {
    int x = sym_make_symbolic("x", 5, 32);
    printf("%d\n", fac(x));
    return 0;
}

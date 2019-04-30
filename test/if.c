// RUN: %symcc -O2 %s %symruntime -o %t
// RUN: %t | FileCheck %s
#include <stdio.h>
#include <stdint.h>

int _sym_build_variable(const char*, int, uint8_t);

int foo(int a, int b) {
    // CHECK: Trying to solve
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

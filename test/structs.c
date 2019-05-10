// RUN: %symcc -O2 %s %symruntime -o %t
// RUN: %t | FileCheck %s
#include <stdio.h>
#include <stdint.h>

int _sym_build_variable(const char*, int, uint8_t);

struct point {
    int x;
    int y;
};

int main(int argc, char* argv[]) {
    int x = _sym_build_variable("x", 5, 32);
    struct point p = {x, 17};

    printf("%s\n", (p.x < 100) ? "yes" : "no");
    // CHECK: Trying to solve
    // CHECK: Found diverging input

    printf("%s\n", (p.y < 100) ? "yes" : "no");
    // CHECK-NOT: Trying to solve

    printf("%s\n", (p.x < p.y) ? "yes" : "no");
    // CHECK: Trying to solve
    // CHECK: Found diverging input

    return 0;
}

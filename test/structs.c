// RUN: %symcc -O2 %s %symruntime -o %t
// RUN: %t | FileCheck %s
#include <stdio.h>
#include <stdint.h>

int _sym_build_variable(const char*, int, uint8_t);

struct point {
    int x;
    int y;
};

static struct point g_point = {1, 2};

int main(int argc, char* argv[]) {
    int x = _sym_build_variable("x", 5, 32);
    struct point p = {x, 17};

    printf("%s\n", (p.x < 100) ? "yes" : "no");
    // CHECK: Trying to solve
    // CHECK: Found diverging input
    // CHECK: yes

    printf("%s\n", (p.y < 100) ? "yes" : "no");
    // CHECK-NOT: Trying to solve
    // CHECK: yes

    printf("%s\n", (p.x < p.y) ? "yes" : "no");
    // CHECK: Trying to solve
    // CHECK: Found diverging input
    // CHECK: yes

    printf("%s\n", ((p.x < g_point.x) || (p.y < g_point.y)) ? "yes" : "no");
    // CHECK: Trying to solve
    // CHECK: Found diverging input
    // CHECK: no

    return 0;
}

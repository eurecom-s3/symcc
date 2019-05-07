// RUN: %symcc -O2 %s %symruntime -o %t
// RUN: %t | FileCheck %s
//
// Test that global variables are handled correctly. The special challenge is
// that we need to initialize the symbolic expression corresponding to any
// global variable that has an initial value.
#include <stdio.h>
#include <stdint.h>

int _sym_build_variable(const char*, int, uint8_t);

int g_increment = 17;
int g_uninitialized;

/* char g_values[] = {1, 2, 3}; */

int increment(int x) {
    int result = x + g_increment;
    if (result < 30)
        return result;
    else
        return 42;
}

/* void sum(int x) { */
/*     int result = 0; */
/*     for (size_t i = 0; i < (sizeof(g_values) / sizeof(g_values[0])); i++) { */
/*         result += g_values[i]; */
/*     } */

/*     printf("%d", (result < x) ? 1 : 0); */
/* } */

int main(int argc, char* argv[]) {
    int x = _sym_build_variable("x", 5, 32);
    printf("%d\n", increment(x));
    // CHECK: Trying to solve
    // CHECK: (bvadd x #x{{0*}}11)

    g_increment = 18;
    printf("%d\n", increment(x));
    // CHECK: Trying to solve
    // CHECK: (bvadd x #x{{0*}}12)

    g_uninitialized = 101;
    printf("%s\n", (x < g_uninitialized) ? "smaller" : "greater or equal");

    /* sum(x); */

    return 0;
}

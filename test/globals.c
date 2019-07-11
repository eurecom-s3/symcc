// RUN: %symcc -O2 %s -o %t
// RUN: %t | FileCheck %s
//
// Test that global variables are handled correctly. The special challenge is
// that we need to initialize the symbolic expression corresponding to any
// global variable that has an initial value.
#include <stdio.h>
#include <stdint.h>

int sym_make_symbolic(const char*, int, uint8_t);

int g_increment = 17;
int g_uninitialized;
int g_more_than_one_byte_int = 512;

char g_values[] = {1, 2, 3};
int g_non_char_values[] = {300, 400, 500};

int increment(int x) {
    int result = x + g_increment;
    if (result < 30)
        return result;
    else
        return 42;
}

void sum(int x) {
    int result = 0;
    for (size_t i = 0; i < (sizeof(g_values) / sizeof(g_values[0])); i++) {
        result += g_values[i];
    }

    printf("%d\n", (result < x) ? 1 : 0);
}

void sum_ints(int x) {
    int result = 0;
    for (size_t i = 0; i < (sizeof(g_non_char_values) / sizeof(g_non_char_values[0])); i++) {
        result += g_non_char_values[i];
    }

    printf("%d\n", (result < x) ? 1 : 0);
}

int main(int argc, char* argv[]) {
    int x = sym_make_symbolic("x", 5, 32);
    printf("%d\n", increment(x));
    // CHECK: Trying to solve
    // CHECK: (bvadd #x{{0*}}11 x)
    // CHECK: Found diverging input

    g_increment = 18;
    printf("%d\n", increment(x));
    // CHECK: Trying to solve
    // CHECK: (bvadd #x{{0*}}12 x)
    // CHECK: Found diverging input

    g_uninitialized = 101;
    printf("%s\n", (x < g_uninitialized) ? "smaller" : "greater or equal");
    // CHECK: Trying to solve
    // CHECK: (bvsle #x{{0*}}65 x)
    // CHECK: Found diverging input

    sum(x);
    // CHECK: Trying to solve
    // CHECK-NOT: Can't find
    // CHECK: Found diverging input

    printf("%s\n", (x < g_more_than_one_byte_int) ? "true" : "false");
    // CHECK: Trying to solve
    // CHECK: #x{{0*}}200
    // CHECK: Can't find

    sum_ints(x);
    // CHECK: Trying to solve
    // CHECK: #x{{0*}}4b0
    // CHECK: Can't find

    return 0;
}

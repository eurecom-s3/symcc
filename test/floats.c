// RUN: %symcc -O2 %s -o %t
// RUN: %t | FileCheck %s
#include <stdio.h>
#include <stdint.h>

int _sym_build_variable(const char*, int, uint8_t);

float g_value = 0.1234;

int main(int argc, char* argv[]) {
    int x = _sym_build_variable("x", 5, 32);

    g_value += x;
    printf("%f\n", g_value);
    // CHECK: 5.1234

    printf("%s\n", ((g_value < 7) && (g_value > 6)) ? "yes" : "no");
    // CHECK: Trying to solve
    // CHECK: Found diverging input
    // CHECK: #x00000006
    // CHECK: no

    return 0;
}

// RUN: %symcc -O2 %s -o %t
// RUN: %t | FileCheck %s
#include <stdio.h>
#include <stdint.h>

int sym_make_symbolic(const char*, int, uint8_t);

volatile int g_value = 0x00ab0012;

int main(int argc, char* argv[]) {
    int x = sym_make_symbolic("x", 5, 32);
    uint8_t *charPtr = (uint8_t*)&g_value;

    charPtr += 2;
    printf("%x\n", *charPtr);
    // CHECK: ab

    printf("%s\n", (*charPtr == x) ? "equal" : "different");
    // CHECK: Trying to solve
    // CHECK: Found diverging input
    // CHECK: #x000000ab
    // CHECK: different

    volatile int local = 0x12345678;
    charPtr = (uint8_t*)&local;
    charPtr++;
    printf("%s\n", (*charPtr == x) ? "equal" : "different");
    // CHECK: Trying to solve
    // CHECK: Found diverging input
    // CHECK: #x00000056
    // CHECK: different

    return 0;
}

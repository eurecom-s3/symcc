// RUN: %symcc -O2 %s -o %t
// RUN: echo -ne "\x05\x00\x00\x00" | %t 2>&1 | %filecheck %s
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

volatile int g_value = 0x00ab0012;

int main(int argc, char* argv[]) {
    int x;
    if (read(STDIN_FILENO, &x, sizeof(x)) != sizeof(x)) {
        printf("Failed to read x\n");
        return -1;
    }
    uint8_t *charPtr = (uint8_t*)&g_value;

    charPtr += 2;
    printf("%x\n", *charPtr);
    // ANY: ab

    printf("%s\n", (*charPtr == x) ? "equal" : "different");
    // SIMPLE: Trying to solve
    // SIMPLE: Found diverging input
    // SIMPLE: #xab
    // QSYM-COUNT-2: SMT
    // QSYM: New testcase
    // ANY: different

    volatile int local = 0x12345678;
    charPtr = (uint8_t*)&local;
    charPtr++;
    printf("%s\n", (*charPtr == x) ? "equal" : "different");
    // SIMPLE: Trying to solve
    // SIMPLE: Found diverging input
    // SIMPLE: #x56
    // QSYM-COUNT-2: SMT
    // QSYM: New testcase
    // ANY: different

    return 0;
}

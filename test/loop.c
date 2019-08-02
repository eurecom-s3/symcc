// RUN: %symcc -O2 %s -o %t
// RUN: echo -ne "\x05\x00\x00\x00" | %t | FileCheck %s
//
// Make sure that our instrumentation works with back-jumps.
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

int fac(int x) {
    int result = 1;

    // CHECK-COUNT-5: Found diverging input
    // CHECK-NOT: Found diverging input
    for (int i = 2; i <= x; i++)
        result *= i;

    return result;
}

int main(int argc, char* argv[]) {
    int x;
    if (read(STDIN_FILENO, &x, sizeof(x)) != sizeof(x)) {
        printf("Failed to read x\n");
        return -1;
    }
    printf("%d\n", fac(x));
    return 0;
}

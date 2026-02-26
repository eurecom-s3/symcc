/*
 * Benchmark target: lightweight crypto/hash verification
 * Simulates a program that checks input against hash-like conditions.
 * Involves arithmetic constraints that are harder for solvers.
 *
 * Compile with SymCC:  symcc -O2 crypto_check.c -o crypto_check_symcc
 * Compile normally:    gcc -O2 crypto_check.c -o crypto_check_native
 * Input: via file (@@) or stdin, 16 bytes expected
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define INPUT_LEN 16
#define ROUNDS 4

/* Simple mixing function (NOT cryptographically secure, just for testing) */
static uint32_t mix(uint32_t a, uint32_t b) {
    a ^= b;
    a = (a << 13) | (a >> 19);
    a *= 0x5bd1e995;
    a ^= a >> 15;
    return a;
}

int main(int argc, char *argv[]) {
    uint8_t input[INPUT_LEN];
    ssize_t n;

    if (argc > 1) {
        int fd = open(argv[1], O_RDONLY);
        if (fd < 0) { perror("open"); return 1; }
        n = read(fd, input, INPUT_LEN);
        close(fd);
    } else {
        n = read(STDIN_FILENO, input, INPUT_LEN);
    }

    if (n < INPUT_LEN) {
        fprintf(stderr, "Need %d bytes\n", INPUT_LEN);
        return 1;
    }

    /* Interpret input as 4 uint32_t values */
    uint32_t v[4];
    memcpy(v, input, 16);

    /* Simple hash/mix rounds */
    uint32_t state = 0x12345678;
    for (int r = 0; r < ROUNDS; r++) {
        state = mix(state, v[r % 4]);
        v[r % 4] = state;
    }

    /* Check conditions on the mixed state */
    /* Level 1: easy */
    if ((v[0] & 0xFF) == 0x42) {
        fprintf(stderr, "Level 1 passed\n");

        /* Level 2: medium */
        if ((v[1] & 0xFFFF) == 0xBEEF) {
            fprintf(stderr, "Level 2 passed\n");

            /* Level 3: harder */
            if (v[2] == 0xDEADC0DE) {
                fprintf(stderr, "Level 3 passed - RARE PATH!\n");
            }
        }
    }

    /* Parallel condition chain - allows testing different path combinations */
    int flags = 0;
    if (input[0] > 0x80) flags |= 1;
    if (input[1] > 0x80) flags |= 2;
    if (input[2] > 0x80) flags |= 4;
    if (input[3] > 0x80) flags |= 8;

    switch (flags) {
    case 0:  fprintf(stderr, "All low\n"); break;
    case 15: fprintf(stderr, "All high\n"); break;
    case 5:  fprintf(stderr, "Alternating\n"); break;
    case 10: fprintf(stderr, "Inverse alternating\n"); break;
    default: fprintf(stderr, "Mixed: %d\n", flags); break;
    }

    /* XOR chain */
    uint8_t xor_result = 0;
    for (int i = 0; i < INPUT_LEN; i++) {
        xor_result ^= input[i];
    }
    if (xor_result == 0) {
        fprintf(stderr, "XOR zero - balanced input\n");
    }

    /* Sum check */
    uint32_t sum = 0;
    for (int i = 0; i < INPUT_LEN; i++) {
        sum += input[i];
    }
    if (sum == 0x400) {
        fprintf(stderr, "Sum match!\n");
    }

    fprintf(stderr, "Done: state=%08x flags=%d xor=%02x sum=%u\n",
            state, flags, xor_result, sum);
    return 0;
}

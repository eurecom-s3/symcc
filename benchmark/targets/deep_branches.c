/*
 * Benchmark target: deep nested branches
 * Creates a deep tree of if-else branches from each input byte.
 * Designed to stress-test constraint solving with many paths.
 *
 * Compile with SymCC:  symcc -O2 deep_branches.c -o deep_branches_symcc
 * Compile normally:    gcc -O2 deep_branches.c -o deep_branches_native
 * Input: via file (@@) or stdin, 8 bytes expected
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>

#define INPUT_LEN 8

static int process_byte(uint8_t b, int depth) {
    int score = 0;

    if (b < 0x20) {
        score += 1;
        if (b < 0x10) {
            score += 10;
            if (b == 0x07) score += 100;
            else if (b == 0x0A) score += 200;
        } else {
            score += 20;
            if (b == 0x1B) score += 150;
        }
    } else if (b < 0x40) {
        score += 2;
        if (b == '0' + depth) score += 300;
        else if (b == 0x2A) score += 50;
    } else if (b < 0x80) {
        score += 3;
        if (b >= 'A' && b <= 'Z') {
            score += 30;
            if (b == 'A' + depth) score += 500;
        } else if (b >= 'a' && b <= 'z') {
            score += 40;
            if (b == 'z' - depth) score += 400;
        }
    } else {
        score += 4;
        if (b == 0xFF) score += 1000;
        else if (b > 0xF0) score += 60;
        else if (b > 0xC0) score += 70;
    }

    return score;
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

    int total = 0;
    for (int i = 0; i < INPUT_LEN; i++) {
        total += process_byte(input[i], i);
    }

    /* Multi-byte conditions */
    uint16_t w0 = (input[0] << 8) | input[1];
    uint16_t w1 = (input[2] << 8) | input[3];
    uint32_t dw = (input[4] << 24) | (input[5] << 16) | (input[6] << 8) | input[7];

    if (w0 == 0x4142) { /* "AB" */
        total += 2000;
        if (w1 == 0x4344) { /* "CD" */
            total += 5000;
            if (dw == 0x45464748) { /* "EFGH" */
                total += 10000;
                fprintf(stderr, "JACKPOT!\n");
            }
        }
    }

    if (total > 5000) {
        fprintf(stderr, "HIGH SCORE: %d\n", total);
    } else if (total > 1000) {
        fprintf(stderr, "Good score: %d\n", total);
    } else {
        fprintf(stderr, "Score: %d\n", total);
    }

    return 0;
}

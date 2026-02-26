/*
 * Benchmark target: maze solver
 * A program with many branching paths based on input bytes.
 * Each byte in the input represents a direction (U/D/L/R) in a maze.
 * This creates an exponentially branching path tree, ideal for
 * testing parallel concolic execution.
 *
 * Compile with SymCC:  symcc -O2 maze.c -o maze_symcc
 * Compile normally:    gcc -O2 maze.c -o maze_native
 * Input: via file (@@) or stdin, 16 bytes expected
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define MAZE_W 8
#define MAZE_H 8
#define INPUT_LEN 16

/* Simple maze: 0=wall, 1=path, 2=goal */
static const uint8_t maze[MAZE_H][MAZE_W] = {
    {1,1,0,0,0,0,0,0},
    {0,1,1,0,0,0,0,0},
    {0,0,1,1,0,0,0,0},
    {0,0,0,1,1,1,0,0},
    {0,0,0,0,0,1,0,0},
    {0,0,0,0,0,1,1,0},
    {0,0,0,0,0,0,1,1},
    {0,0,0,0,0,0,0,2},
};

int main(int argc, char *argv[]) {
    uint8_t input[INPUT_LEN];
    ssize_t n;

    if (argc > 1 && strcmp(argv[1], "@@") != 0) {
        int fd = open(argv[1], O_RDONLY);
        if (fd < 0) { perror("open"); return 1; }
        n = read(fd, input, INPUT_LEN);
        close(fd);
    } else {
        n = read(STDIN_FILENO, input, INPUT_LEN);
    }

    if (n < INPUT_LEN) {
        fprintf(stderr, "Need %d bytes of input\n", INPUT_LEN);
        return 1;
    }

    int x = 0, y = 0;
    int steps = 0;

    for (int i = 0; i < INPUT_LEN; i++) {
        int dir = input[i] % 4;  /* 0=up, 1=down, 2=left, 3=right */
        int nx = x, ny = y;

        if (dir == 0 && y > 0) ny--;
        else if (dir == 1 && y < MAZE_H-1) ny++;
        else if (dir == 2 && x > 0) nx--;
        else if (dir == 3 && x < MAZE_W-1) nx++;

        if (nx >= 0 && nx < MAZE_W && ny >= 0 && ny < MAZE_H) {
            if (maze[ny][nx] != 0) {
                x = nx;
                y = ny;
                steps++;
            }
        }

        if (maze[y][x] == 2) {
            fprintf(stderr, "GOAL REACHED in %d steps!\n", steps);
            return 0;
        }
    }

    fprintf(stderr, "Ended at (%d,%d) after %d steps\n", x, y, steps);
    return 0;
}

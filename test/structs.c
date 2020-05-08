// RUN: %symcc -O2 %s -o %t
// RUN: echo -ne "\x05\x00\x00\x00" | %t 2>&1 | %filecheck %s
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

struct point {
    int x;
    int y;
};

struct line {
    struct point start;
    struct point end;
};

static struct point g_point = {1, 2};
static struct point g_point_array[] = {{1, 2}, {3, 4}, {5, 6}};

int main(int argc, char* argv[]) {
    int x;
    if (read(STDIN_FILENO, &x, sizeof(x)) != sizeof(x)) {
        printf("Failed to read x\n");
        return -1;
    }

    struct point p = {x, 17};

    printf("%s\n", (p.x < 100) ? "yes" : "no");
    // SIMPLE: Trying to solve
    // SIMPLE: Found diverging input
    // QSYM-COUNT-2: SMT
    // QSYM: New testcase
    // ANY: yes

    printf("%s\n", (p.y < 100) ? "yes" : "no");
    // SIMPLE-NOT: Trying to solve
    // QSYM-NOT: SMT
    // ANY: yes

    printf("%s\n", (p.x < p.y) ? "yes" : "no");
    // SIMPLE: Trying to solve
    // SIMPLE: Found diverging input
    // QSYM-COUNT-2: SMT
    // QSYM: New testcase
    // ANY: yes

    printf("%s\n", ((p.x < g_point.x) || (p.y < g_point.y)) ? "yes" : "no");
    // SIMPLE: Trying to solve
    // SIMPLE: Found diverging input
    // QSYM-COUNT-2: SMT
    // QSYM: New testcase
    // ANY: no

    printf("%s\n", (g_point_array[1].x < x) ? "yes" : "no");
    // SIMPLE: Trying to solve
    // SIMPLE: Found diverging input
    // QSYM-COUNT-2: SMT
    // QSYM: New testcase
    // ANY: yes

    // Nested structs

    struct line l = {{0, 0}, {5, 5}};

    printf("%s\n", (l.end.x > x) ? "yes" : "no");
    // SIMPLE: Trying to solve
    // SIMPLE: Found diverging input
    // QSYM-COUNT-2: SMT
    // QSYM: New testcase
    // ANY: no

    return 0;
}

// This file is part of SymCC.
//
// SymCC is free software: you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// SymCC is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with
// SymCC. If not, see <https://www.gnu.org/licenses/>.

// RUN: %symcc -O2 %s -o %t
// RUN: echo -ne "\x00\x00\x00\x05" | %t 2>&1 | %filecheck %s

#include <stdint.h>
#include <stdio.h>

#include <arpa/inet.h>
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
        fprintf(stderr, "Failed to read x\n");
        return -1;
    }
    x = ntohl(x);

    struct point p = {x, 17};

    fprintf(stderr, "%s\n", (p.x < 100) ? "yes" : "no");
    // SIMPLE: Trying to solve
    // SIMPLE: Found diverging input
    // QSYM-COUNT-2: SMT
    // QSYM: New testcase
    // ANY: yes

    fprintf(stderr, "%s\n", (p.y < 100) ? "yes" : "no");
    // SIMPLE-NOT: Trying to solve
    // QSYM-NOT: SMT
    // ANY: yes

    fprintf(stderr, "%s\n", (p.x < p.y) ? "yes" : "no");
    // SIMPLE: Trying to solve
    // SIMPLE: Found diverging input
    // QSYM-COUNT-2: SMT
    // QSYM: New testcase
    // ANY: yes

    fprintf(stderr, "%s\n", ((p.x < g_point.x) || (p.y < g_point.y)) ? "yes" : "no");
    // SIMPLE: Trying to solve
    // SIMPLE: Found diverging input
    // QSYM-COUNT-2: SMT
    // QSYM: New testcase
    // ANY: no

    fprintf(stderr, "%s\n", (g_point_array[1].x < x) ? "yes" : "no");
    // SIMPLE: Trying to solve
    // SIMPLE: Found diverging input
    // QSYM-COUNT-2: SMT
    // QSYM: New testcase
    // ANY: yes

    // Nested structs

    struct line l = {{0, 0}, {5, 5}};

    fprintf(stderr, "%s\n", (l.end.x > x) ? "yes" : "no");
    // SIMPLE: Trying to solve
    // SIMPLE: Found diverging input
    // QSYM-COUNT-2: SMT
    // QSYM: New testcase
    // ANY: no

    return 0;
}

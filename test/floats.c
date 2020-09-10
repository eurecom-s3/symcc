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

float g_value = 0.1234;

int main(int argc, char *argv[]) {
  int x;
  if (read(STDIN_FILENO, &x, sizeof(x)) != sizeof(x)) {
    fprintf(stderr, "Failed to read x\n");
    return -1;
  }
  x = ntohl(x);

  g_value += x;
  fprintf(stderr, "%f\n", g_value);
  // ANY: 5.1234

  fprintf(stderr, "%s\n", ((g_value < 7) && (g_value > 6)) ? "yes" : "no");
  // SIMPLE: Trying to solve
  // SIMPLE: Found diverging input
  // SIMPLE: #x06
  // Qsym doesn't support symbolic floats!
  // QSYM-NOT: SMT
  // ANY: no

  return 0;
}

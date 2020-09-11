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

// RUN: %symcc %s -o %t
// RUN: echo -ne "\x04\x03\x02\x01" | %t 2>&1 | %filecheck %s
// RUN: %symcc %s -S -emit-llvm -o - | FileCheck --check-prefix=BITCODE %s
//
// Here we test that the "bswap" intrinsic is handled correctly.

#include <stdint.h>
#include <stdio.h>

#include <arpa/inet.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
  uint32_t x;
  if (read(STDIN_FILENO, &x, sizeof(x)) != sizeof(x)) {
    fprintf(stderr, "Failed to read x\n");
    return -1;
  }
  x = ntohl(x);

  // BITCODE: llvm.bswap.i32
  uint32_t y = __builtin_bswap32(x);

  // ANY: 0x04030201 0x01020304
  fprintf(stderr, "0x%08x 0x%08x\n", x, y);

  // SIMPLE: Trying to solve
  // SIMPLE: Found diverging input
  // SIMPLE-DAG: stdin0 -> #xef
  // SIMPLE-DAG: stdin1 -> #xbe
  // SIMPLE-DAG: stdin2 -> #xfe
  // SIMPLE-DAG: stdin3 -> #xca
  // QSYM-COUNT-2: SMT
  // ANY: Not quite.
  if (y == 0xcafebeef)
    fprintf(stderr, "Correct test input.\n");
  else
    fprintf(stderr, "Not quite.\n");

  return 0;
}

// clang-format off
// RUN: %symcc -O1 %s -o %t
// RUN: echo -ne "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" | %t 2>&1 | %filecheck %s
// clang-format on
//
// Test vector operations end-to-end:
//   - vector construction (insertelement, createValueExpression for vectors)
//   - element extraction (extractelement, constant and variable index)
//   - vector arithmetic (binary op slow path)
//   - vector bitwise ops (binary op fast path)
//   - shufflevector
//   - vector comparison (icmp on vectors)

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

typedef int v4si __attribute__((vector_size(16)));

int main() {
  int32_t input[4];
  uint8_t idx_byte;
  if (read(STDIN_FILENO, input, sizeof(input)) != sizeof(input)) {
    fprintf(stderr, "Failed to read input\n");
    return -1;
  }
  if (read(STDIN_FILENO, &idx_byte, sizeof(idx_byte)) != sizeof(idx_byte)) {
    fprintf(stderr, "Failed to read index byte\n");
    return -1;
  }

  // Build vector from scalars: tests insertelement + createValueExpression.
  v4si vec = {input[0], input[1], input[2], input[3]};

  // --- Vector arithmetic: slow-path decomposition (add).
  v4si doubled = vec + vec;
  // SIMPLE: Trying to solve
  // SIMPLE: Found diverging input
  if (doubled[0] == 100) {
    fprintf(stderr, "Add Bingo\n");
  } else {
    fprintf(stderr, "Add Nope\n");
  }
  // ANY: Add Nope

  // --- Vector multiplication: slow-path decomposition (mul).
  v4si tripled = vec * (v4si){3, 3, 3, 3};
  // SIMPLE: Trying to solve
  // SIMPLE: Found diverging input
  if (tripled[2] == 99) {
    fprintf(stderr, "Mul Bingo\n");
  } else {
    fprintf(stderr, "Mul Nope\n");
  }
  // ANY: Mul Nope

  // --- Vector bitwise XOR: fast path (bits don't interact across lanes).
  v4si masked = vec ^ (v4si){0xFF, 0xFF, 0xFF, 0xFF};
  // SIMPLE: Trying to solve
  // SIMPLE: Found diverging input
  if (masked[1] == 0xCC) {
    fprintf(stderr, "Xor Bingo\n");
  } else {
    fprintf(stderr, "Xor Nope\n");
  }
  // ANY: Xor Nope

  // --- Shuffle: reverses the lane order.
  v4si shuffled = __builtin_shufflevector(vec, vec, 3, 2, 1, 0);
  // SIMPLE: Trying to solve
  // SIMPLE: Found diverging input
  if (shuffled[0] == 42) {
    fprintf(stderr, "Shuf Bingo\n");
  } else {
    fprintf(stderr, "Shuf Nope\n");
  }
  // ANY: Shuf Nope

  // --- Vector comparison: produces <4 x i1> result.
  v4si eq_mask = (vec == (v4si){7, 7, 7, 7});
  // SIMPLE: Trying to solve
  // SIMPLE: Found diverging input
  if (eq_mask[0] != 0) {
    fprintf(stderr, "CmpEq Bingo\n");
  } else {
    fprintf(stderr, "CmpEq Nope\n");
  }
  // ANY: CmpEq Nope

  // --- Variable-index extractelement: exercises the ITE chain so the
  // solver can jointly reason about index selection and lane contents.
  unsigned idx = ((unsigned)idx_byte) & 3;
  int32_t picked = vec[idx];
  // SIMPLE: Trying to solve
  // SIMPLE: Found diverging input
  if (picked == 1337) {
    fprintf(stderr, "Pick Bingo\n");
  } else {
    fprintf(stderr, "Pick Nope\n");
  }
  // ANY: Pick Nope

  return 0;
}

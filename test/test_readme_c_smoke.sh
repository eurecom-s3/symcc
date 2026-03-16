#!/usr/bin/env bash
set -euo pipefail

WORKDIR="${1:-/tmp/symcc-readme-c-smoke}"
ROUNDS="${2:-4}"

mkdir -p "$WORKDIR/results"
cd "$WORKDIR"

cat > test.c <<'EOF'
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

int foo(int a, int b) {
  if (2 * a < b)
    return a;
  else if (a % b)
    return b;
  else
    return a + b;
}

int main(int argc, char* argv[]) {
  int x;
  if (read(STDIN_FILENO, &x, sizeof(x)) != sizeof(x)) {
    printf("Failed to read x\n");
    return -1;
  }
  printf("%d\n", foo(x, 7));
  return 0;
}
EOF

symcc -O2 test.c -o test
export SYMCC_OUTPUT_DIR="$WORKDIR/results"

# Seed input from README.
printf 'aaaa' > seed.bin
./test < seed.bin >/dev/null

# Symbolic smoke: run seed + generated inputs for a few rounds.
outputs="$WORKDIR/symbolic_outputs.txt"
: > "$outputs"

for _ in $(seq 1 "$ROUNDS"); do
  for tc in "$WORKDIR"/results/*; do
    [ -f "$tc" ] || continue

    out=$(./test < "$tc" | tr -d '\r\n')
    printf '%s\n' "$out" >> "$outputs"

    ./test < "$tc" >/dev/null
  done
done

count_cases=$(ls -1 "$WORKDIR/results" | wc -l)
[ "$count_cases" -ge 2 ]
distinct_symbolic=$(sort -u "$outputs" | wc -l)
[ "$distinct_symbolic" -ge 2 ]

# Deterministic branch checks for the three foo() outcomes.
printf '\x01\x00\x00\x00' > in_a.bin      # 2*a < 7  => return a
printf '\x08\x00\x00\x00' > in_b.bin      # a % 7 != 0 => return b (=7)
printf '\x07\x00\x00\x00' > in_apb.bin    # else => return a + b (=14)

[ "$(./test < in_a.bin | tr -d '\r\n')" = "1" ]
[ "$(./test < in_b.bin | tr -d '\r\n')" = "7" ]
[ "$(./test < in_apb.bin | tr -d '\r\n')" = "14" ]

echo "PASS: README C testcase smoke test passed (symbolic + all 3 branch outcomes)"

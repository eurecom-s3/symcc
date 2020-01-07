// RUN: %symcc -O2 %s -o %t
// RUN: /usr/bin/echo -ne "\x05\x00\x00\x00" > %T/%basename_t.input
// RUN: env SYMCC_INPUT_FILE=%T/%basename_t.input %t %T/%basename_t.input 2>&1 | %filecheck %s

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
  int fd = open(argv[1], O_RDONLY);
  if (fd < 0) {
    perror("failed to open input file");
    return -1;
  }

  int input;
  if (read(fd, &input, sizeof(input)) < 0) {
    perror("failed to read from the input file");
    return -1;
  }

  // SIMPLE: Trying to solve
  // QSYM-COUNT-2: SMT
  // ANY: Not sure
  // ANY-NOT: Warning
  if (input == 42)
    printf("This is the answer.");
  else
    printf("Not sure this is correct...");

  return 0;
}

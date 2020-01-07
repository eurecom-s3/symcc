// RUN: %symcc -O2 %s -o %t
// RUN: /usr/bin/echo -ne "\x05\x00\x00\x00aaaa" > %T/%basename_t.input
// RUN: env SYMCC_INPUT_FILE=%T/%basename_t.input %t %T/%basename_t.input 2>&1 | %filecheck %s

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
  //
  // Read from the input file using Unix primitives.
  //

  // ANY-NOT: Warning
  int fd = open(argv[1], O_RDONLY);
  if (fd < 0) {
    perror("failed to open the input file");
    return -1;
  }

  int input;
  if (read(fd, &input, sizeof(input)) < 0) {
    perror("failed to read from the input file");
    return -1;
  }

  // SIMPLE: Trying to solve
  // SIMPLE: Found diverging input
  // QSYM-COUNT-2: SMT
  // QSYM: New testcase
  // ANY: Not sure
  if (input >= 42)
    printf("This may be the answer.\n");
  else
    printf("Not sure this is correct...\n");

  //
  // Rewind and read again.
  //

  if (lseek(fd, 0, SEEK_SET) == (off_t)-1) {
    perror("failed to rewind the file");
    return -1;
  }

  if (read(fd, &input, sizeof(input)) < 0) {
    perror("failed to read from the input file");
    return -1;
  }

  // SIMPLE: Trying to solve
  // SIMPLE: Can't find
  // QSYM-COUNT-2: SMT
  // QSYM: New testcase: {{.*}}-optimistic
  // ANY: No.
  if (input >= 42)
    printf("Again, this may be it.\n");
  else
    printf("No.\n");

  //
  // Read with the C standard library.
  //

  // ANY: Warning
  FILE *file = fopen(argv[1], "r");
  if (file == NULL) {
    perror("failed to open the input file");
    return -1;
  }

  int same_input;
  if (fread(&same_input, sizeof(same_input), 1, file) < 0) {
    perror("failed to read from the input file");
    return -1;
  }

  // SIMPLE: Trying to solve
  // QSYM-COUNT-2: SMT
  // ANY: Yep
  if (same_input == 5)
    printf("Yep, it's the test input.\n");
  else
    printf("Not the test input!\n");

  //
  // Rewind and read again.
  //

  if (fseek(file, 0, SEEK_SET) == -1) {
    perror("failed to rewind the file");
    return -1;
  }

  if (fread(&same_input, sizeof(same_input), 1, file) < 0) {
    perror("failed to read from the input file");
    return -1;
  }

  // SIMPLE: Trying to solve
  // SIMPLE-NOT: stdin4
  // QSYM-COUNT-2: SMT
  // ANY: Still
  if (same_input == 5)
    printf("Still the test input.\n");
  else
    printf("Not the test input!\n");

  return 0;
}

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
int main(int argc, char *argv[]) {

  char    buf[1024];
  ssize_t i;
  if ((i = read(0, buf, sizeof(buf) - 1)) < 24) return 0;
  buf[i] = 0;
  if (buf[0] != 'A') return 0;
  if (buf[1] != 'B') return 0;
  if (buf[2] != 'C') return 0;
  if (buf[3] != 'D') return 0;
  if (memcmp(buf + 4, "1234", 4) || memcmp(buf + 8, "EFGH", 4)) return 0;
  if (strlen(buf + 12) == 5) {
    printf("The string length after buf + 12 is: %lu\n", strlen(buf+12));
    printf("HIT!\n");
  } else {
    printf("The string length after buf + 12 is: %lu\n", strlen(buf+12));
    printf("strchr(buf+12) is: %s\n", strchr(buf+12, '\0'));
    printf("NOT HIT!\n");
  }

  return 0;

}

/*
 * Benchmark target: simple protocol parser
 * Parses a 32-byte "message" with header, type field, length, checksum,
 * and payload. Many nested conditions create a rich constraint tree.
 *
 * Compile with SymCC:  symcc -O2 parser.c -o parser_symcc
 * Compile normally:    gcc -O2 parser.c -o parser_native
 * Input: via file (@@) or stdin, 32 bytes expected
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define MSG_LEN 32

struct message {
    uint8_t  magic[4];    /* "SYM\x01" */
    uint8_t  msg_type;    /* 1=PING, 2=DATA, 3=CTRL, 4=AUTH */
    uint8_t  flags;
    uint16_t length;      /* payload length (LE) */
    uint32_t checksum;    /* XOR of all payload bytes */
    uint8_t  payload[20];
};

static uint32_t compute_checksum(const uint8_t *data, int len) {
    uint32_t cs = 0;
    for (int i = 0; i < len; i++) {
        cs ^= (uint32_t)data[i] << ((i % 4) * 8);
    }
    return cs;
}

int main(int argc, char *argv[]) {
    uint8_t buf[MSG_LEN];
    ssize_t n;

    if (argc > 1) {
        int fd = open(argv[1], O_RDONLY);
        if (fd < 0) { perror("open"); return 1; }
        n = read(fd, buf, MSG_LEN);
        close(fd);
    } else {
        n = read(STDIN_FILENO, buf, MSG_LEN);
    }

    if (n < MSG_LEN) {
        fprintf(stderr, "Need %d bytes\n", MSG_LEN);
        return 1;
    }

    struct message *msg = (struct message *)buf;

    /* Check magic bytes */
    if (msg->magic[0] != 'S') { fprintf(stderr, "bad magic[0]\n"); return 1; }
    if (msg->magic[1] != 'Y') { fprintf(stderr, "bad magic[1]\n"); return 1; }
    if (msg->magic[2] != 'M') { fprintf(stderr, "bad magic[2]\n"); return 1; }
    if (msg->magic[3] != 0x01) { fprintf(stderr, "bad magic[3]\n"); return 1; }

    uint16_t len = msg->length;
    if (len > 20) { fprintf(stderr, "length too large\n"); return 1; }

    /* Verify checksum */
    uint32_t expected_cs = compute_checksum(msg->payload, len);
    if (msg->checksum != expected_cs) {
        fprintf(stderr, "checksum mismatch: got %08x expected %08x\n",
                msg->checksum, expected_cs);
        return 1;
    }

    /* Dispatch by message type */
    switch (msg->msg_type) {
    case 1: /* PING */
        fprintf(stderr, "PING received\n");
        if (msg->flags & 0x01) fprintf(stderr, "  urgent\n");
        if (msg->flags & 0x02) fprintf(stderr, "  encrypted\n");
        break;

    case 2: /* DATA */
        fprintf(stderr, "DATA received, %d bytes\n", len);
        if (len >= 4) {
            uint32_t val = *(uint32_t *)msg->payload;
            if (val == 0xDEADBEEF) fprintf(stderr, "  found marker!\n");
            else if (val == 0xCAFEBABE) fprintf(stderr, "  found java!\n");
            else if (val > 1000000) fprintf(stderr, "  large value\n");
            else fprintf(stderr, "  val=%u\n", val);
        }
        break;

    case 3: /* CTRL */
        fprintf(stderr, "CTRL received\n");
        if (msg->payload[0] == 'R') fprintf(stderr, "  RESET\n");
        else if (msg->payload[0] == 'S') fprintf(stderr, "  STOP\n");
        else if (msg->payload[0] == 'G') fprintf(stderr, "  GO\n");
        break;

    case 4: /* AUTH */
        fprintf(stderr, "AUTH received\n");
        if (len >= 8) {
            /* Check "password" */
            if (memcmp(msg->payload, "p@ssw0rd", 8) == 0) {
                fprintf(stderr, "  authenticated!\n");
            } else {
                fprintf(stderr, "  auth failed\n");
            }
        }
        break;

    default:
        fprintf(stderr, "unknown type %d\n", msg->msg_type);
        return 1;
    }

    fprintf(stderr, "OK\n");
    return 0;
}

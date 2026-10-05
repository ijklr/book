#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

enum { MAX_FRAME = 4096 };

/* A reply must contain every requested byte; EOF is truncation here. */
static int read_full(int fd, void *buffer, size_t length)
{
    unsigned char *bytes = buffer;
    size_t offset = 0;
    while (offset < length) {
        ssize_t n = read(fd, bytes + offset, length - offset);
        if (n > 0)
            offset += (size_t)n;
        else if (n < 0 && errno == EINTR)
            continue;
        else {
            if (n == 0)
                errno = EPROTO;
            return -1;
        }
    }
    return 0;
}

static int write_full(int fd, const void *buffer, size_t length)
{
    const unsigned char *bytes = buffer;
    size_t offset = 0;
    while (offset < length) {
        ssize_t n = write(fd, bytes + offset, length - offset);
        if (n > 0)
            offset += (size_t)n;
        else if (n < 0 && errno == EINTR)
            continue;
        else {
            if (n == 0)
                errno = EIO;
            return -1;
        }
    }
    return 0;
}

static int exchange(int fd, const char *message, size_t length)
{
    uint32_t header = htonl((uint32_t)length);
    if (write_full(fd, &header, sizeof header) < 0 ||
        write_full(fd, message, length) < 0) {
        perror("write request");
        return -1;
    }
    if (read_full(fd, &header, sizeof header) < 0) {
        perror("read reply header");
        return -1;
    }
    uint32_t reply_length = ntohl(header);
    if (reply_length > MAX_FRAME || reply_length != length) {
        fprintf(stderr, "invalid reply length\n");
        return -1;
    }
    unsigned char reply[MAX_FRAME];
    if (read_full(fd, reply, reply_length) < 0) {
        perror("read reply body");
        return -1;
    }
    if (memcmp(reply, message, length) != 0) {
        fprintf(stderr, "reply differs from request\n");
        return -1;
    }
    if (fwrite(reply, 1, reply_length, stdout) != reply_length ||
        putchar('\n') == EOF || fflush(stdout) == EOF) {
        perror("stdout");
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s PORT MESSAGE\n", argv[0]);
        return EXIT_FAILURE;
    }
    char *end;
    errno = 0;
    unsigned long port = strtoul(argv[1], &end, 10);
    if (argv[1][0] < '0' || argv[1][0] > '9' || *end != '\0' ||
        errno != 0 || port == 0 || port > 65535) {
        fprintf(stderr, "invalid port\n");
        return EXIT_FAILURE;
    }
    size_t length = strlen(argv[2]);
    if (length > MAX_FRAME) {
        fprintf(stderr, "message exceeds %d bytes\n", MAX_FRAME);
        return EXIT_FAILURE;
    }
    struct sigaction action = {0};
    action.sa_handler = SIG_IGN;
    if (sigemptyset(&action.sa_mask) < 0 ||
        sigaction(SIGPIPE, &action, NULL) < 0) {
        perror("ignore SIGPIPE");
        return EXIT_FAILURE;
    }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1) {
        fprintf(stderr, "invalid built-in address\n");
        (void)close(fd);
        return EXIT_FAILURE;
    }
    address.sin_port = htons((uint16_t)port);
    int status = EXIT_FAILURE;
    /* An interrupted connect is failed, not blindly retried. */
    if (connect(fd, (struct sockaddr *)&address, sizeof address) < 0)
        perror("connect");
    else if (exchange(fd, argv[2], length) == 0)
        status = EXIT_SUCCESS;
    if (close(fd) < 0) {
        perror("close");
        status = EXIT_FAILURE;
    }
    return status;
}

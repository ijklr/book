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

/* 1: complete; 0: EOF before bytes; -2: partial EOF; -1: error. */
static int read_full(int fd, void *buffer, size_t length)
{
    unsigned char *bytes = buffer;
    size_t offset = 0;
    while (offset < length) {
        ssize_t n = read(fd, bytes + offset, length - offset);
        if (n > 0)
            offset += (size_t)n;
        else if (n == 0)
            return offset == 0 ? 0 : -2;
        else if (errno != EINTR)
            return -1;
    }
    return 1;
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

static int serve(int fd)
{
    uint32_t header;
    int result = read_full(fd, &header, sizeof header);
    if (result == 0)
        return 0; /* Clean EOF before the one permitted request. */
    if (result != 1) {
        if (result == -1)
            perror("read header");
        else
            fprintf(stderr, "truncated header\n");
        return -1;
    }
    uint32_t length = ntohl(header);
    if (length > MAX_FRAME) {
        fprintf(stderr, "frame exceeds %d bytes\n", MAX_FRAME);
        return -1;
    }
    unsigned char body[MAX_FRAME];
    result = read_full(fd, body, length);
    if (result != 1) {
        if (result == -1)
            perror("read body");
        else
            fprintf(stderr, "truncated body\n");
        return -1;
    }
    if (write_full(fd, &header, sizeof header) < 0 ||
        write_full(fd, body, length) < 0) {
        perror("write reply");
        return -1;
    }
    return 0;
}

int main(void)
{
    struct sigaction action = {0};
    action.sa_handler = SIG_IGN;
    if (sigemptyset(&action.sa_mask) < 0 ||
        sigaction(SIGPIPE, &action, NULL) < 0) {
        perror("ignore SIGPIPE");
        return EXIT_FAILURE;
    }
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1) {
        fprintf(stderr, "invalid built-in address\n");
        (void)close(listener);
        return EXIT_FAILURE;
    }
    address.sin_port = htons(0);
    int status = EXIT_FAILURE;
    if (bind(listener, (struct sockaddr *)&address, sizeof address) < 0 ||
        listen(listener, 1) < 0) {
        perror("bind/listen");
        goto done;
    }
    socklen_t address_length = sizeof address;
    if (getsockname(listener, (struct sockaddr *)&address, &address_length) < 0) {
        perror("getsockname");
        goto done;
    }
    if (printf("PORT %u\n", (unsigned)ntohs(address.sin_port)) < 0 ||
        fflush(stdout) == EOF) {
        perror("print port");
        goto done;
    }
    int connection;
    do {
        connection = accept(listener, NULL, NULL);
    } while (connection < 0 && errno == EINTR);
    if (connection < 0) {
        perror("accept");
        goto done;
    }
    status = serve(connection) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    if (close(connection) < 0) {
        perror("close connection");
        status = EXIT_FAILURE;
    }
done:
    if (close(listener) < 0) {
        perror("close listener");
        status = EXIT_FAILURE;
    }
    return status;
}

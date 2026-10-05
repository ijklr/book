#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

enum { TOTAL_BYTES = 65536, CHUNK = 1024, WAIT_MS = 5000 };

static int nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int wait_ready(int fd, short events, short *returned)
{
    struct pollfd item = { .fd = fd, .events = events, .revents = 0 };
    int result;
    do {
        result = poll(&item, 1, WAIT_MS);
    } while (result < 0 && errno == EINTR);
    if (result == 0) {
        errno = ETIMEDOUT;
        return -1;
    }
    if (result < 0)
        return -1;
    *returned = item.revents;
    return 0;
}

static int produce(int fd)
{
    unsigned char buffer[CHUNK];
    size_t sent = 0;
    while (sent < TOTAL_BYTES) {
        size_t length = TOTAL_BYTES - sent;
        if (length > sizeof buffer)
            length = sizeof buffer;
        for (size_t i = 0; i < length; ++i)
            buffer[i] = (unsigned char)((sent + i) % 251);
        ssize_t n = write(fd, buffer, length);
        if (n > 0) {
            sent += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            short events;
            if (wait_ready(fd, POLLOUT, &events) < 0)
                return -1;
            if (events & (POLLERR | POLLHUP | POLLNVAL)) {
                errno = EPIPE;
                return -1;
            }
            continue;
        }
        if (n == 0)
            errno = EIO;
        return -1;
    }
    return 0;
}

static int consume(int fd, size_t *count, uint64_t *checksum)
{
    unsigned char buffer[CHUNK];
    for (;;) {
        short events;
        if (wait_ready(fd, POLLIN, &events) < 0)
            return -1;
        if (events & (POLLERR | POLLNVAL)) {
            errno = EIO;
            return -1;
        }
        if (!(events & (POLLIN | POLLHUP)))
            continue;
        for (;;) {
            ssize_t n = read(fd, buffer, sizeof buffer);
            if (n > 0) {
                if ((size_t)n > TOTAL_BYTES - *count) {
                    errno = EOVERFLOW;
                    return -1;
                }
                for (ssize_t i = 0; i < n; ++i)
                    *checksum += buffer[i];
                *count += (size_t)n;
            } else if (n == 0) {
                return 0; /* Only read(), not POLLHUP, establishes EOF. */
            } else if (errno == EINTR) {
                continue;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else {
                return -1;
            }
        }
    }
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
    int ends[2];
    if (pipe(ends) < 0) {
        perror("pipe");
        return EXIT_FAILURE;
    }
    if (nonblocking(ends[0]) < 0 || nonblocking(ends[1]) < 0) {
        perror("nonblocking");
        (void)close(ends[0]);
        (void)close(ends[1]);
        return EXIT_FAILURE;
    }
    pid_t child = fork();
    if (child < 0) {
        perror("fork");
        (void)close(ends[0]);
        (void)close(ends[1]);
        return EXIT_FAILURE;
    }
    if (child == 0) {
        (void)close(ends[0]);
        int result = produce(ends[1]);
        if (result < 0)
            perror("produce");
        if (close(ends[1]) < 0)
            result = -1;
        _exit(result == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
    }
    (void)close(ends[1]);
    size_t count = 0;
    uint64_t checksum = 0;
    int result = consume(ends[0], &count, &checksum);
    if (result < 0)
        perror("consume");
    if (close(ends[0]) < 0) {
        perror("close reader");
        result = -1;
    }
    int child_status;
    pid_t waited;
    do {
        waited = waitpid(child, &child_status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0) {
        perror("waitpid");
        return EXIT_FAILURE;
    }
    if (result < 0 || !WIFEXITED(child_status) ||
        WEXITSTATUS(child_status) != EXIT_SUCCESS || count != TOTAL_BYTES)
        return EXIT_FAILURE;
    if (printf("bytes=%zu checksum=%" PRIu64 "\n", count, checksum) < 0 ||
        fflush(stdout) == EOF) {
        perror("stdout");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

static int write_all(int fd, const unsigned char *data, size_t len)
{
    size_t done = 0;
    while (done < len) {
        ssize_t n = write(fd, data + done, len - done);
        if (n > 0) {
            done += (size_t)n;
        } else if (n == -1 && errno == EINTR) {
            continue;
        } else {
            if (n == 0) {
                errno = EIO; /* Avoid spinning if no progress is possible. */
            }
            return -1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) {
        fprintf(stderr, "usage: copyfd < INPUT > OUTPUT\n");
        return 2;
    }
    struct sigaction action = {0};
    action.sa_handler = SIG_IGN;
    if (sigemptyset(&action.sa_mask) == -1 ||
        sigaction(SIGPIPE, &action, NULL) == -1) {
        perror("copyfd: sigaction");
        return 1;
    }

    unsigned char data[16384];
    int result = 0;
    for (;;) {
        ssize_t n = read(STDIN_FILENO, data, sizeof data);
        if (n > 0) {
            if (write_all(STDOUT_FILENO, data, (size_t)n) == -1) {
                perror("copyfd: write");
                result = 1;
                break;
            }
        } else if (n == 0) {
            break;
        } else if (errno != EINTR) {
            perror("copyfd: read");
            result = 1;
            break;
        }
    }

    /* These are one-shot closes, not EINTR retry loops. */
    if (close(STDOUT_FILENO) == -1) {
        perror("copyfd: close stdout");
        result = 1;
    }
    if (close(STDIN_FILENO) == -1) {
        perror("copyfd: close stdin");
        result = 1;
    }
    return result;
}

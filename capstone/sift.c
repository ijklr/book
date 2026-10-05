#include "matcher.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct output {
    const char *label;
    int found;
    int failed;
};

/* Escape labels so one result always occupies one unambiguous output line. */
static int print_label(const char *label)
{
    const unsigned char *p = (const unsigned char *)label;
    for (; *p != '\0'; ++p) {
        if (*p < 32 || *p >= 127 || *p == ':' || *p == '\\') {
            if (fprintf(stdout, "\\x%02x", (unsigned int)*p) < 0) {
                return -1;
            }
        } else if (fputc(*p, stdout) == EOF) {
            return -1;
        }
    }
    return 0;
}

static int report_match(uintmax_t line, void *context)
{
    struct output *out = context;
    if (print_label(out->label) == -1 ||
        fprintf(stdout, ":%" PRIuMAX "\n", line) < 0) {
        out->failed = 1;
        if (errno == 0) {
            errno = EIO;
        }
        return -1;
    }
    out->found = 1;
    return 0;
}

static int search_fd(int fd, struct matcher *m, struct output *out)
{
    unsigned char buffer[8192];
    matcher_reset(m);
    for (;;) {
        ssize_t n = read(fd, buffer, sizeof buffer);
        if (n > 0) {
            if (matcher_feed(m, buffer, (size_t)n, report_match, out) == -1) {
                return -1;
            }
        } else if (n == 0) {
            return 0;
        } else if (errno != EINTR) {
            return -1;
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: sift PATTERN [FILE ...]\n");
        return 2;
    }
    struct sigaction action = {0};
    action.sa_handler = SIG_IGN;
    if (sigemptyset(&action.sa_mask) == -1 ||
        sigaction(SIGPIPE, &action, NULL) == -1) {
        perror("sift: sigaction");
        return 2;
    }
    struct matcher *m = matcher_create(argv[1]);
    if (m == NULL) {
        perror("sift: pattern (1..4096 bytes, no newline)");
        return 2;
    }
    int had_error = 0;
    struct output out = {NULL, 0, 0};
    int inputs = argc > 2 ? argc - 2 : 1;
    for (int i = 0; i < inputs && !out.failed; ++i) {
        const char *path = argc > 2 ? argv[i + 2] : "-";
        int standard_input = strcmp(path, "-") == 0;
        int fd = STDIN_FILENO;
        out.label = standard_input ? "(stdin)" : path;
        if (!standard_input) {
            do {
                fd = open(path, O_RDONLY);
            } while (fd == -1 && errno == EINTR);
            if (fd == -1) {
                fprintf(stderr, "sift: %s: %s\n", path, strerror(errno));
                had_error = 1;
                continue;
            }
        }
        if (search_fd(fd, m, &out) == -1) {
            fprintf(stderr, "sift: %s: %s\n",
                    out.failed ? "stdout" : out.label, strerror(errno));
            had_error = 1;
        }
        /* No blind close retry: see the platform discussion in Chapter 3. */
        if (!standard_input && close(fd) == -1) {
            fprintf(stderr, "sift: close %s: %s\n", path, strerror(errno));
            had_error = 1;
        }
    }
    if (fflush(stdout) == EOF) {
        perror("sift: flush stdout");
        had_error = 1;
    }
    matcher_destroy(m);
    return had_error ? 2 : (out.found ? 0 : 1);
}

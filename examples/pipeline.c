#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static _Noreturn void child_failed(int code)
{
    static const char message[] = "pipeline: child setup or exec failed\n";
    size_t done = 0;
    while (done < sizeof message - 1) {
        ssize_t n = write(STDERR_FILENO, message + done,
                          sizeof message - 1 - done);
        if (n > 0) {
            done += (size_t)n;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
    _exit(code);
}

static _Noreturn void start_child(char **args, int source, int target, int ends[2])
{
    int result;
    do {
        result = dup2(source, target);
    } while (result == -1 && errno == EINTR);
    if (result == -1) {
        child_failed(126);
    }
    /* Both ends are above descriptor 2; neither is the new target. */
    if (close(ends[0]) == -1 || close(ends[1]) == -1) {
        child_failed(126);
    }
    execvp(args[0], args);
    int saved = errno;
    child_failed(saved == ENOENT ? 127 : 126);
}

static int close_end(int fd)
{
    if (close(fd) == -1) {
        perror("close pipe");
        return -1;
    }
    return 0;
}

static int collect(pid_t child, const char *label)
{
    int status;
    pid_t result;
    do {
        result = waitpid(child, &status, 0);
    } while (result == -1 && errno == EINTR);
    if (result == -1) {
        perror("waitpid");
        return 1;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        fprintf(stderr, "pipeline: %s terminated by signal %d\n", label, sig);
        return 128 + sig;
    }
    fprintf(stderr, "pipeline: unexpected child status\n");
    return 1;
}

int main(int argc, char **argv)
{
    int split = 1;
    while (split < argc && strcmp(argv[split], "--") != 0) {
        ++split;
    }
    if (split == 1 || split >= argc - 1) {
        fprintf(stderr, "usage: %s COMMAND [ARG...] -- COMMAND [ARG...]\n",
                argv[0]);
        return 2;
    }
    argv[split] = NULL;
    for (int fd = 0; fd <= 2; ++fd) {
        if (fcntl(fd, F_GETFD) == -1) {
            fprintf(stderr, "pipeline: standard descriptors must be open\n");
            return 1;
        }
    }
    if (fflush(NULL) == EOF) {
        perror("fflush");
        return 1;
    }

    int ends[2];
    if (pipe(ends) == -1) {
        perror("pipe");
        return 1;
    }
    if (fcntl(ends[0], F_SETFD, FD_CLOEXEC) == -1 ||
        fcntl(ends[1], F_SETFD, FD_CLOEXEC) == -1) {
        perror("fcntl");
        (void)close_end(ends[0]);
        (void)close_end(ends[1]);
        return 1;
    }

    pid_t left = fork();
    if (left == -1) {
        perror("fork left");
        (void)close_end(ends[0]);
        (void)close_end(ends[1]);
        return 1;
    }
    if (left == 0) {
        start_child(&argv[1], ends[1], STDOUT_FILENO, ends);
    }

    pid_t right = fork();
    if (right == -1) {
        perror("fork right");
        (void)close_end(ends[0]);
        (void)close_end(ends[1]);
        /* Startup cannot finish. Terminate and reap the child we own. */
        if (kill(left, SIGKILL) == -1 && errno != ESRCH) {
            perror("kill left");
        }
        (void)collect(left, "left command");
        return 1;
    }
    if (right == 0) {
        start_child(&argv[split + 1], ends[0], STDIN_FILENO, ends);
    }

    int close_read = close_end(ends[0]);
    int close_write = close_end(ends[1]);
    if (close_read == -1 || close_write == -1) {
        /* A failed close may retain an end and prevent the peer's EOF. */
        if (kill(left, SIGKILL) == -1 && errno != ESRCH) {
            perror("kill left");
        }
        if (kill(right, SIGKILL) == -1 && errno != ESRCH) {
            perror("kill right");
        }
        (void)collect(left, "left command");
        (void)collect(right, "right command");
        return 1;
    }
    int left_status = collect(left, "left command");
    int right_status = collect(right, "right command");
    return right_status != 0 ? right_status : left_status;
}

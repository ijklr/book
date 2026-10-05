#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static _Noreturn void child_exec_failed(int code)
{
    static const char message[] = "run: exec failed\n";
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

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s COMMAND [ARG...]\n", argv[0]);
        return 2;
    }
    if (fflush(NULL) == EOF) {
        perror("fflush");
        return 1;
    }

    pid_t child = fork();
    if (child == -1) {
        perror("fork");
        return 1;
    }
    if (child == 0) {
        execvp(argv[1], &argv[1]);
        int saved = errno;
        child_exec_failed(saved == ENOENT ? 127 : 126);
    }

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
        int signal_number = WTERMSIG(status);
        fprintf(stderr, "run: child terminated by signal %d\n", signal_number);
        return 128 + signal_number;
    }
    fprintf(stderr, "run: unexpected child status\n");
    return 1;
}

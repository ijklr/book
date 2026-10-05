#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    sigset_t signals;
    if (sigemptyset(&signals) == -1 ||
        sigaddset(&signals, SIGINT) == -1 ||
        sigaddset(&signals, SIGTERM) == -1) {
        perror("signal set");
        return 1;
    }
    if (sigprocmask(SIG_BLOCK, &signals, NULL) == -1) {
        perror("sigprocmask");
        return 1;
    }

    /* A background shell command can inherit an ignored SIGINT. */
    struct sigaction action = {0};
    action.sa_handler = SIG_DFL;
    if (sigemptyset(&action.sa_mask) == -1 ||
        sigaction(SIGINT, &action, NULL) == -1 ||
        sigaction(SIGTERM, &action, NULL) == -1) {
        perror("sigaction");
        return 1;
    }
    if (printf("ready pid=%jd\n", (intmax_t)getpid()) < 0 ||
        fflush(stdout) == EOF) {
        perror("stdout");
        return 1;
    }

    int received;
    int error = sigwait(&signals, &received);
    if (error != 0) {
        fprintf(stderr, "sigwait: %s\n", strerror(error));
        return 1;
    }
    const char *name = received == SIGINT ? "SIGINT" : "SIGTERM";
    if (printf("received %s; cleanup complete\n", name) < 0 ||
        fflush(stdout) == EOF) {
        perror("stdout");
        return 1;
    }
    /* Leave these signals blocked while main returns and stdio shuts down. */
    return 0;
}

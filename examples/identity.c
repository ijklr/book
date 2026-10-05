#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    long status = 0;
    if (argc > 2) {
        fprintf(stderr, "usage: identity [STATUS: 0..125]\n");
        return 2;
    }
    if (argc == 2) {
        char *end;
        errno = 0;
        status = strtol(argv[1], &end, 10);
        if (errno != 0 || end == argv[1] || *end != '\0' ||
            status < 0 || status > 125) {
            fprintf(stderr, "identity: STATUS must be an integer from 0 to 125\n");
            return 2;
        }
    }

    printf("pid=%jd ppid=%jd uid=%ju gid=%ju\n",
           (intmax_t)getpid(), (intmax_t)getppid(),
           (uintmax_t)getuid(), (uintmax_t)getgid());
    for (int i = 0; i < argc; ++i) {
        printf("argv[%d]=%s\n", i, argv[i]);
    }
    printf("requested-status=%ld\n", status);
    if (fflush(stdout) == EOF || ferror(stdout)) {
        perror("identity: stdout");
        return 1;
    }
    return (int)status;
}

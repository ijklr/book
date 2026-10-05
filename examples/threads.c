#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WORKERS = 4, ITERATIONS = 25000 };

struct shared {
    pthread_mutex_t mutex;
    unsigned long count;
};

static void check_thread(int error, const char *operation)
{
    if (error != 0) {
        fprintf(stderr, "%s: %s\n", operation, strerror(error));
        exit(EXIT_FAILURE);
    }
}

static void *worker(void *argument)
{
    struct shared *shared = argument;
    for (unsigned i = 0; i < ITERATIONS; ++i) {
        check_thread(pthread_mutex_lock(&shared->mutex), "mutex lock");
        ++shared->count;
        check_thread(pthread_mutex_unlock(&shared->mutex), "mutex unlock");
    }
    return NULL;
}

int main(void)
{
    struct shared shared = { .count = 0 };
    pthread_t workers[WORKERS];
    size_t started = 0;
    int create_error = 0;

    check_thread(pthread_mutex_init(&shared.mutex, NULL), "mutex init");
    for (; started < WORKERS; ++started) {
        create_error = pthread_create(&workers[started], NULL, worker, &shared);
        if (create_error != 0)
            break;
    }
    for (size_t i = 0; i < started; ++i)
        check_thread(pthread_join(workers[i], NULL), "join");
    check_thread(pthread_mutex_destroy(&shared.mutex), "mutex destroy");
    if (create_error != 0) {
        fprintf(stderr, "create: %s\n", strerror(create_error));
        return EXIT_FAILURE;
    }
    const unsigned long expected = (unsigned long)WORKERS * ITERATIONS;
    if (printf("count=%lu expected=%lu\n", shared.count, expected) < 0 ||
        fflush(stdout) == EOF) {
        perror("stdout");
        return EXIT_FAILURE;
    }
    return shared.count == expected ? EXIT_SUCCESS : EXIT_FAILURE;
}

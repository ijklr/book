#include "matcher.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

struct results {
    uintmax_t lines[16];
    size_t count;
};

static int collect(uintmax_t line, void *context)
{
    struct results *r = context;
    assert(r->count < sizeof r->lines / sizeof r->lines[0]);
    r->lines[r->count++] = line;
    return 0;
}

static int fail_output(uintmax_t line, void *context)
{
    (void)line;
    (void)context;
    errno = EIO;
    return -1;
}

int main(void)
{
    const unsigned char input[] = "ababababac twice ababac\n\naba\nbac\n\0ababac\nababac";
    const size_t length = sizeof input - 1;
    struct matcher *m = matcher_create("ababac");
    assert(m != NULL);
    /* Every possible two-chunk partition must produce the same result. */
    for (size_t cut = 0; cut <= length; ++cut) {
        struct results r = {{0}, 0};
        matcher_reset(m);
        assert(matcher_feed(m, input, cut, collect, &r) == 0);
        assert(matcher_feed(m, input + cut, length - cut, collect, &r) == 0);
        assert(r.count == 3);
        assert(r.lines[0] == 1 && r.lines[1] == 5 && r.lines[2] == 6);
    }
    /* Exercise more than two boundaries and one-byte feeds. */
    for (size_t chunk = 1; chunk <= length; ++chunk) {
        struct results r = {{0}, 0};
        matcher_reset(m);
        for (size_t offset = 0; offset < length;) {
            size_t n = length - offset < chunk ? length - offset : chunk;
            assert(matcher_feed(m, input + offset, n, collect, &r) == 0);
            offset += n;
        }
        assert(r.count == 3);
        assert(r.lines[0] == 1 && r.lines[1] == 5 && r.lines[2] == 6);
    }
    matcher_reset(m);
    assert(matcher_feed(m, input, length, fail_output, NULL) == -1);
    assert(errno == EIO);
    matcher_reset(m);
    struct results r = {{0}, 0};
    assert(matcher_feed(m, NULL, 0, collect, &r) == 0);
    assert(matcher_feed(m, input, length, collect, &r) == 0);
    assert(r.count == 3);
    matcher_destroy(m);
    matcher_destroy(NULL);

    const char *invalid[] = {NULL, "", "a\nb"};
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        errno = 0;
        assert(matcher_create(invalid[i]) == NULL);
        assert(errno == EINVAL);
    }
    char oversized[4098];
    memset(oversized, 'a', sizeof oversized - 1);
    oversized[sizeof oversized - 1] = '\0';
    assert(matcher_create(oversized) == NULL && errno == EINVAL);
    oversized[4096] = '\0';
    m = matcher_create(oversized);
    assert(m != NULL);
    matcher_destroy(m);
    puts("matcher: all chunk boundaries, reset, and callback failures passed");
    return 0;
}

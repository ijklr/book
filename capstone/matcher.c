#include "matcher.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

struct matcher {
    unsigned char *pattern;
    size_t *prefix;
    size_t length;
    size_t matched;
    uintmax_t line;
    int reported;
};

void matcher_destroy(struct matcher *m)
{
    if (m != NULL) {
        free(m->pattern);
        free(m->prefix);
        free(m);
    }
}

void matcher_reset(struct matcher *m)
{
    m->matched = 0;
    m->line = 1;
    m->reported = 0;
}

struct matcher *matcher_create(const char *pattern)
{
    if (pattern == NULL) {
        errno = EINVAL;
        return NULL;
    }
    size_t n = strlen(pattern);
    if (n == 0 || n > 4096 || strchr(pattern, '\n') != NULL ||
        n > SIZE_MAX / sizeof(size_t)) {
        errno = EINVAL;
        return NULL;
    }
    struct matcher *m = calloc(1, sizeof *m);
    if (m == NULL) {
        return NULL;
    }
    *m = (struct matcher){0};
    m->pattern = malloc(n);
    if (m->pattern == NULL) {
        int saved = errno;
        matcher_destroy(m);
        errno = saved;
        return NULL;
    }
    m->prefix = calloc(n, sizeof *m->prefix);
    if (m->prefix == NULL) {
        int saved = errno;
        matcher_destroy(m);
        errno = saved;
        return NULL;
    }
    memcpy(m->pattern, pattern, n);
    m->length = n;
    for (size_t i = 1, k = 0; i < n; ++i) {
        while (k > 0 && m->pattern[i] != m->pattern[k]) {
            k = m->prefix[k - 1];
        }
        if (m->pattern[i] == m->pattern[k]) {
            ++k;
        }
        m->prefix[i] = k;
    }
    matcher_reset(m);
    return m;
}

int matcher_feed(struct matcher *m, const unsigned char *data, size_t length,
                 match_callback emit, void *context)
{
    for (size_t i = 0; i < length; ++i) {
        unsigned char byte = data[i];
        if (byte == '\n') {
            if (m->line == UINTMAX_MAX) {
                errno = EOVERFLOW;
                return -1;
            }
            ++m->line;
            m->matched = 0;
            m->reported = 0;
            continue;
        }
        if (m->reported) {
            continue;
        }
        while (m->matched > 0 && byte != m->pattern[m->matched]) {
            m->matched = m->prefix[m->matched - 1];
        }
        if (byte == m->pattern[m->matched]) {
            ++m->matched;
        }
        if (m->matched == m->length) {
            m->reported = 1;
            m->matched = 0;
            if (emit(m->line, context) == -1) {
                return -1;
            }
        }
    }
    return 0;
}

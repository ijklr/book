#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct buffer {
    unsigned char *data;
    size_t len;
    size_t cap;
};

static int buffer_reserve(struct buffer *b, size_t needed)
{
    assert(b->len <= b->cap);
    assert((b->cap == 0) == (b->data == NULL));
    if (needed <= b->cap) {
        return 0;
    }
    size_t cap = b->cap == 0 ? 64 : b->cap;
    while (cap < needed) {
        if (cap > SIZE_MAX / 2) {
            cap = needed;
            break;
        }
        cap *= 2;
    }
    unsigned char *replacement = realloc(b->data, cap);
    if (replacement == NULL) {
        return -1;
    }
    b->data = replacement;
    b->cap = cap;
    return 0;
}

/* src must refer to n readable bytes outside b's allocation. */
static int buffer_append(struct buffer *b, const void *src, size_t n)
{
    if (n == 0) {
        return 0;
    }
    if (n > SIZE_MAX - b->len) {
        return -1;
    }
    size_t needed = b->len + n;
    if (buffer_reserve(b, needed) == -1) {
        return -1;
    }
    memcpy(b->data + b->len, src, n);
    b->len = needed;
    return 0;
}

static void buffer_destroy(struct buffer *b)
{
    free(b->data);
    *b = (struct buffer){0};
}

int main(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) {
        fprintf(stderr, "usage: buffer < INPUT > OUTPUT\n");
        return 2;
    }
    struct buffer b = {0};
    unsigned char chunk[4096];
    int result = 0;
    for (;;) {
        size_t n = fread(chunk, 1, sizeof chunk, stdin);
        if (n != 0 && buffer_append(&b, chunk, n) == -1) {
            fprintf(stderr, "buffer: cannot grow input buffer\n");
            result = 1;
            break;
        }
        if (n < sizeof chunk) {
            if (ferror(stdin)) {
                fprintf(stderr, "buffer: input error\n");
                result = 1;
            }
            break;
        }
    }
    if (result == 0) {
        fprintf(stderr, "bytes=%zu capacity=%zu\n", b.len, b.cap);
        if ((b.len != 0 && fwrite(b.data, 1, b.len, stdout) != b.len) ||
            fflush(stdout) == EOF || ferror(stdout)) {
            fprintf(stderr, "buffer: output error\n");
            result = 1;
        }
    }
    buffer_destroy(&b);
    return result;
}

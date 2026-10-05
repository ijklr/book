#ifndef SIFT_MATCHER_H
#define SIFT_MATCHER_H

#include <stddef.h>
#include <stdint.h>

struct matcher;
typedef int (*match_callback)(uintmax_t line, void *context);

/* Owns a copy of a nonempty, newline-free pattern of at most 4096 bytes.
 * Returns NULL and sets errno on invalid input or allocation failure. */
struct matcher *matcher_create(const char *pattern);
void matcher_reset(struct matcher *m);
void matcher_destroy(struct matcher *m);

/* Borrows data for this call; preserves state across chunks. Calls emit once
 * per matching line, with one-based line numbers. Returns 0 or -1 (errno).
 * A callback must return 0, or -1 with errno set. After failure, reset or
 * destroy the matcher. m and emit must be valid; data may be NULL only if
 * length is zero, and the input bytes must remain unchanged until return.
 * The same matcher must not be used concurrently. A callback must not feed,
 * reset, or destroy the active matcher; its lifetime must extend through
 * the entire feed call. The callback may operate on a different matcher. */
int matcher_feed(struct matcher *m, const unsigned char *data, size_t length,
                 match_callback emit, void *context);

#endif

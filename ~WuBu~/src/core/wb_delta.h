/*
 * wb_delta.h -- delta compression (LP-28).
 *
 * A snapshot delta encodes only the 32-bit words that changed since the last
 * acknowledged state: a bitmap of changed words followed by those words. For a
 * typical entity snapshot most words are unchanged, so this is the difference
 * between sending a few kilobytes and sending the whole world every tick.
 *
 * The format is self-describing by length alone (the word count comes from the
 * state length), which keeps the codec tiny and deterministic.
 *
 * C11, no deps.
 */
#ifndef WB_DELTA_H
#define WB_DELTA_H

#include <stddef.h>

/* Encode the delta from `old_state` to `new_state` (both `len` bytes) into
 * `out` (capacity `out_cap`). Returns the bytes written, or 0 on bad input or
 * insufficient capacity. */
size_t wb_delta_encode(const void *old_state, const void *new_state, size_t len,
                       void *out, size_t out_cap);

/* Apply a delta to `old_state` (both `len` bytes) into `out`. Returns the bytes
 * written (always `len` on success), or 0 on bad input or a malformed delta. */
size_t wb_delta_apply(const void *old_state, const void *delta, size_t len,
                      void *out, size_t out_cap);

/* The exact encoded size for a state of `len` bytes (header + worst case). */
size_t wb_delta_max_size(size_t len);

#endif /* WB_DELTA_H */

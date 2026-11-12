/*
 * wb_qkey.h -- quadkey spatial queries (LP-108 stone 1).
 *
 * A quadkey already answers "which cell is this point in". It does not answer the
 * questions a reservation ledger actually asks: does this volume contain that
 * one, do two volumes overlap, are they neighbours, and what is the smallest
 * volume that covers them both?
 *
 * All four follow from one property of a hierarchical grid: two cells are either
 * DISJOINT or one CONTAINS the other. There is no partial overlap -- a quadkey
 * prefix relationship IS the containment relationship. So:
 *
 *   contains(a, b)  :=  a is a prefix of b (or equal)
 *   overlaps(a, b)  :=  contains(a, b) || contains(b, a)
 *   enclose(a, b)   :=  the longest common prefix of a and b
 *
 * Level 0 is the whole world and has the empty quadkey "", which contains
 * everything -- useful for "the entire planet".
 *
 * TWO QUADKEYS. This module speaks the DIGIT STRING form: a quadkey is a
 * `const char *` of '0'..'3'. `wb_spatial.h` speaks a DIFFERENT form: a quadkey
 * is a `uint64_t` morton-coded handle in the typedef `wb_qkey`. They are two
 * representations of the same grid and they do not convert into each other
 * anywhere in the tree -- the tile store and the spatial core use the handle,
 * and the nesting/geo layer built here uses the string. That split is known debt
 * (see docs/EXPERIENCE-ASSESSMENT.md); until it is closed, the two must not share
 * a function name. Hence `wb_qkey_depth`/`wb_qkey_up` here rather than
 * `wb_qkey_level`/`wb_qkey_parent`, which belong to the handle.
 *
 * C11, no deps. Deterministic.
 */
#ifndef WB_QKEY_H
#define WB_QKEY_H

#include <stddef.h>
#include <stdint.h>

#define WB_QKEY_MAX 64

/* Encode (level, col, row) as a quadkey of '0'..'3'. Level 0 is the root and
 * encodes to the empty string. Returns 0 on success, -1 if the coordinates are
 * out of range for the level or the buffer is too small. */
int wb_qkey_encode(uint32_t level, uint32_t col, uint32_t row, char *out, size_t cap);

/* Decode a quadkey. Returns 0 on success, -1 on an invalid digit or an over-long
 * key. Any output pointer may be NULL. */
int wb_qkey_decode(const char *qk, uint32_t *level, uint32_t *col, uint32_t *row);

/* The depth (length) of a quadkey string, or 0 if it is not a valid key. Note that
 * a valid root is also depth 0, so callers that care should wb_qkey_decode.
 * Named `depth` rather than `level` because wb_spatial.h's handle-based quadkey
 * already owns wb_qkey_level(wb_qkey) -- see the note at the top of this file. */
uint32_t wb_qkey_depth(const char *qk);

int wb_qkey_equal(const char *a, const char *b);
int wb_qkey_contains(const char *a, const char *b);   /* a contains b, or they are equal */
int wb_qkey_overlaps(const char *a, const char *b);
int wb_qkey_adjacent(const char *a, const char *b);   /* same level, sharing an edge */

/* The smallest quadkey containing both a and b. Returns 0 on success. */
int wb_qkey_enclose(const char *a, const char *b, char *out, size_t cap);

/* The parent of a quadkey string (one level up). Returns 0 on success, -1 if `qk`
 * is already the root. Named `up` for the same reason `depth` is not `level`. */
int wb_qkey_up(const char *qk, char *out, size_t cap);

/* 1 if `qk` is a valid quadkey. */
int wb_qkey_valid(const char *qk);

#endif /* WB_QKEY_H */

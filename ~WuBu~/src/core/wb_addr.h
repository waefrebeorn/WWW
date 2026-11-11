/*
 * wb_addr.h -- hierarchical place addresses (LP-104 stone 1).
 *
 * A place is addressed by a PATH of named levels, deepest last:
 *
 *   universe/world/continent/city/patch/stadium
 *
 * The path is the address. It is canonicalized on parse -- lowercased, no empty
 * segments, no leading or trailing separator, a fixed character set -- so
 * "Earth/Europe" and "earth/europe" are the same place and two servers that both
 * hold the name agree on it without comparing structs.
 *
 * Because it is a path, the prefix relationship IS the containment relationship:
 * "earth" contains "earth/europe", which contains "earth/europe/helsinki". That
 * makes an address usable directly as a nesting key -- the same shape as the
 * quadkey, at the granularity of named places rather than grid cells.
 *
 * C11, no deps. Deterministic.
 */
#ifndef WB_ADDR_H
#define WB_ADDR_H

#include <stddef.h>
#include <stdint.h>

#define WB_ADDR_MAX_SEG 8
#define WB_ADDR_SEG_MAX 32
#define WB_ADDR_MAX 256

typedef struct {
    char     seg[WB_ADDR_MAX_SEG][WB_ADDR_SEG_MAX];
    uint32_t n;
} wb_addr;

/* Parse and canonicalize an address. Returns 0 on success, -1 if the string is
 * malformed, has too many or too few segments, a segment is too long or empty, or
 * a character outside [A-Za-z0-9_-] appears. */
int wb_addr_parse(const char *s, wb_addr *a);

/* Format an address canonically. Returns 0 on success, -1 if it does not fit. */
int wb_addr_format(const wb_addr *a, char *out, size_t cap);

int wb_addr_equal(const wb_addr *a, const wb_addr *b);

/* a contains b: a is a prefix of b, or they are equal. */
int wb_addr_contains(const wb_addr *a, const wb_addr *b);

/* a is a PROPER prefix of b -- a strict ancestor, not equality. */
int wb_addr_is_ancestor(const wb_addr *a, const wb_addr *b);

/* The longest common prefix of a and b. Returns 0 on success. */
int wb_addr_common(const wb_addr *a, const wb_addr *b, wb_addr *out);

uint32_t wb_addr_depth(const wb_addr *a);
int      wb_addr_segment(const wb_addr *a, uint32_t i, char *out, size_t cap);

/* 1 if the string is a valid canonical address. */
int wb_addr_valid(const char *s);

#endif /* WB_ADDR_H */

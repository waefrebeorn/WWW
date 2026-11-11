/*
 * wb_interest.h -- parent-child interest propagation (LP-103 stone 1).
 *
 * Interest is computed at the leaf (the patch) but it is CONSUMED at every level.
 * A server holding a parent cell must know it is interesting because a child is;
 * a client holding a parent cell must be able to cull the whole subtree when the
 * parent is uninteresting. Neither works if interest lives only at the leaves.
 *
 * One rule covers all of it: a cell is interesting when the set contains it OR
 * any ANCESTOR of it. So:
 *
 *   covers(s, c)  :=  some member of s is c or an ancestor of c
 *   aggregate(s)  :=  s plus every ancestor of every member   (push interest UP)
 *   refine(s)     :=  the immediate children of every member  (push interest DOWN)
 *   coarsen(s, L) :=  the ancestor of every member that sits at level L
 *
 * Sets are deduplicated and kept in insertion order, so two servers computing the
 * same interest produce the same set.
 *
 * C11, no deps. Deterministic.
 */
#ifndef WB_INTEREST_H
#define WB_INTEREST_H

#include <stdint.h>

#include "wb_qkey.h"

#define WB_INTEREST_MAX 64

typedef struct {
    char     cells[WB_INTEREST_MAX][WB_QKEY_MAX];
    uint32_t n;
} wb_interest_set;

void wb_interest_init(wb_interest_set *s);
uint32_t wb_interest_count(const wb_interest_set *s);

/* Add a cell. Returns 0 on success, -1 if the set is full. A duplicate is
 * accepted silently and changes nothing. */
int  wb_interest_add(wb_interest_set *s, const char *cell);

/* 1 if `cell` is in the set or is a descendant of a member. */
int  wb_interest_covers(const wb_interest_set *s, const char *cell);

/* s plus every ancestor of every member -- the parent-cell AOI. */
int  wb_interest_aggregate(const wb_interest_set *s, wb_interest_set *out);

/* The immediate children of every member -- one level of detail down. */
int  wb_interest_refine(const wb_interest_set *s, wb_interest_set *out);

/* The ancestor of every member that sits at exactly `level`. Members shallower
 * than `level` are dropped. */
int  wb_interest_coarsen(const wb_interest_set *s, uint32_t level, wb_interest_set *out);

int  wb_interest_equal(const wb_interest_set *a, const wb_interest_set *b);

#endif /* WB_INTEREST_H */

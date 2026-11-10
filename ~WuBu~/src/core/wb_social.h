/*
 * wb_social.h -- the social graph (LP-51 stone 1).
 *
 * Identity is already half-built in wb_avatar_identity (JSON records); the graph
 * is not. The graph is a set of directed edges between users carrying three
 * flags -- friend, blocked, pending -- and every question about it is answered
 * from those edges rather than from a cached counter that can drift.
 *
 * The rules that matter:
 *   - Friendship is MUTUAL. a->b having the friend bit is a request, not a
 *     friendship; both directions must carry it.
 *   - Blocking is asymmetric and absolute: it clears any friendship in both
 *     directions and pins trust at zero.
 *   - Trust is derived, never stored: 0 blocked, 1 direct friend, 2 friend of a
 *     friend, 3 stranger. Deriving it means it cannot go stale.
 *
 * C11, no deps. Deterministic: iteration is in ascending user order.
 */
#ifndef WB_SOCIAL_H
#define WB_SOCIAL_H

#include <stdint.h>

typedef enum {
    WB_SOCIAL_FRIEND  = 1u << 0,
    WB_SOCIAL_BLOCKED = 1u << 1,
    WB_SOCIAL_PENDING = 1u << 2
} wb_social_flag;

/* Trust ranks, derived from the edges. */
typedef enum {
    WB_SOCIAL_TRUST_BLOCKED = 0,   /* a block in either direction */
    WB_SOCIAL_TRUST_FRIEND  = 1,   /* mutual friends */
    WB_SOCIAL_TRUST_FOF     = 2,   /* a friend of a friend */
    WB_SOCIAL_TRUST_STRANGER= 3    /* no path of length <= 2 */
} wb_social_rank;

typedef struct {
    uint32_t user, other;
    uint32_t flags;
} wb_social_edge;

typedef struct {
    wb_social_edge *edges;
    uint32_t        n, cap;
    uint32_t        n_users;
} wb_social;

int  wb_social_init(wb_social *g, uint32_t n_users, uint32_t cap);
void wb_social_free(wb_social *g);
void wb_social_clear(wb_social *g);

/* Replace the flags on the edge user->other (creating it if needed). */
int  wb_social_set(wb_social *g, uint32_t user, uint32_t other, uint32_t flags);
/* Add flags to an edge. Returns the resulting flags, or 0 if out of range. */
int  wb_social_add(wb_social *g, uint32_t user, uint32_t other, uint32_t flags);
/* Remove flags from an edge. Returns the resulting flags, or 0 if out of range. */
int  wb_social_remove(wb_social *g, uint32_t user, uint32_t other, uint32_t flags);

/* 1 if every bit in `flags` is set on the edge user->other. */
int  wb_social_has(const wb_social *g, uint32_t user, uint32_t other, uint32_t flags);
/* The raw flags on the edge, or 0 if there is no edge. */
uint32_t wb_social_flags(const wb_social *g, uint32_t user, uint32_t other);

int  wb_social_are_friends(const wb_social *g, uint32_t a, uint32_t b);
int  wb_social_blocked(const wb_social *g, uint32_t a, uint32_t b);

/* Block `other` from `user`: set the block and clear any friendship both ways. */
int  wb_social_block(wb_social *g, uint32_t user, uint32_t other);

/* The mutual friends of `user`, ascending, into `out`. Returns the count. */
uint32_t wb_social_friends(const wb_social *g, uint32_t user, uint32_t *out, uint32_t cap);

/* The derived trust rank from `from` towards `to`. */
int  wb_social_trust(const wb_social *g, uint32_t from, uint32_t to);

uint32_t wb_social_count(const wb_social *g);

#endif /* WB_SOCIAL_H */

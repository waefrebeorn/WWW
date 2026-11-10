/*
 * wb_social.c -- the social graph (LP-51 stone 1). See wb_social.h.
 */
#include "wb_social.h"

#include <stdlib.h>
#include <string.h>

int wb_social_init(wb_social *g, uint32_t n_users, uint32_t cap) {
    if (!g || cap == 0) return -1;
    g->edges = (wb_social_edge *)calloc(cap, sizeof(wb_social_edge));
    if (!g->edges) return -1;
    g->n = 0;
    g->cap = cap;
    g->n_users = n_users;
    return 0;
}

void wb_social_free(wb_social *g) {
    if (!g) return;
    free(g->edges);
    g->edges = NULL;
    g->n = g->cap = g->n_users = 0;
}

void wb_social_clear(wb_social *g) {
    if (!g) return;
    g->n = 0;
}

static int wb_social_in_range(const wb_social *g, uint32_t u) {
    return g && g->n_users && u < g->n_users;
}

static wb_social_edge *wb_social_find(wb_social *g, uint32_t user, uint32_t other) {
    if (!g || !g->edges) return NULL;
    for (uint32_t i = 0; i < g->n; i++)
        if (g->edges[i].user == user && g->edges[i].other == other) return &g->edges[i];
    return NULL;
}

static const wb_social_edge *wb_social_find_c(const wb_social *g, uint32_t user, uint32_t other) {
    if (!g || !g->edges) return NULL;
    for (uint32_t i = 0; i < g->n; i++)
        if (g->edges[i].user == user && g->edges[i].other == other) return &g->edges[i];
    return NULL;
}

int wb_social_set(wb_social *g, uint32_t user, uint32_t other, uint32_t flags) {
    if (!wb_social_in_range(g, user) || !wb_social_in_range(g, other)) return -1;
    if (user == other) return -1;
    wb_social_edge *e = wb_social_find(g, user, other);
    if (e) { e->flags = flags; return 0; }
    if (g->n == g->cap) return -1;
    e = &g->edges[g->n++];
    e->user = user; e->other = other; e->flags = flags;
    return 0;
}

int wb_social_add(wb_social *g, uint32_t user, uint32_t other, uint32_t flags) {
    if (!wb_social_in_range(g, user) || !wb_social_in_range(g, other)) return 0;
    if (user == other) return 0;
    wb_social_edge *e = wb_social_find(g, user, other);
    if (!e) {
        if (g->n == g->cap) return 0;
        e = &g->edges[g->n++];
        e->user = user; e->other = other; e->flags = 0;
    }
    e->flags |= flags;
    return (int)e->flags;
}

int wb_social_remove(wb_social *g, uint32_t user, uint32_t other, uint32_t flags) {
    if (!wb_social_in_range(g, user) || !wb_social_in_range(g, other)) return 0;
    wb_social_edge *e = wb_social_find(g, user, other);
    if (!e) return 0;
    e->flags &= ~flags;
    return (int)e->flags;
}

int wb_social_has(const wb_social *g, uint32_t user, uint32_t other, uint32_t flags) {
    const wb_social_edge *e = wb_social_find_c(g, user, other);
    return e && (e->flags & flags) == flags;
}

uint32_t wb_social_flags(const wb_social *g, uint32_t user, uint32_t other) {
    const wb_social_edge *e = wb_social_find_c(g, user, other);
    return e ? e->flags : 0u;
}

int wb_social_are_friends(const wb_social *g, uint32_t a, uint32_t b) {
    return wb_social_has(g, a, b, WB_SOCIAL_FRIEND) && wb_social_has(g, b, a, WB_SOCIAL_FRIEND);
}

int wb_social_blocked(const wb_social *g, uint32_t a, uint32_t b) {
    return wb_social_has(g, a, b, WB_SOCIAL_BLOCKED) || wb_social_has(g, b, a, WB_SOCIAL_BLOCKED);
}

int wb_social_block(wb_social *g, uint32_t user, uint32_t other) {
    if (!wb_social_in_range(g, user) || !wb_social_in_range(g, other)) return -1;
    if (user == other) return -1;
    /* the block wins: the block direction becomes exactly BLOCKED (dropping any
     * pending request), and the reverse direction loses both the friendship it
     * may have had and any request still waiting */
    wb_social_set(g, user, other, WB_SOCIAL_BLOCKED);
    wb_social_remove(g, other, user, WB_SOCIAL_FRIEND | WB_SOCIAL_PENDING);
    return 0;
}

uint32_t wb_social_friends(const wb_social *g, uint32_t user, uint32_t *out, uint32_t cap) {
    if (!g || !out || !wb_social_in_range(g, user)) return 0;
    /* collect the mutual friends first, then emit them in ascending order so the
     * result does not depend on the order edges happened to be added */
    uint32_t *tmp = (uint32_t *)malloc((size_t)g->n_users * sizeof(uint32_t));
    if (!tmp) return 0;
    uint32_t n = 0;
    for (uint32_t v = 0; v < g->n_users; v++)
        if (v != user && wb_social_are_friends(g, user, v)) tmp[n++] = v;
    uint32_t written = 0;
    for (uint32_t i = 0; i < n && written < cap; i++) out[written++] = tmp[i];
    free(tmp);
    return written;
}

int wb_social_trust(const wb_social *g, uint32_t from, uint32_t to) {
    if (!wb_social_in_range(g, from) || !wb_social_in_range(g, to)) return WB_SOCIAL_TRUST_STRANGER;
    if (from == to) return WB_SOCIAL_TRUST_FRIEND;
    if (wb_social_blocked(g, from, to)) return WB_SOCIAL_TRUST_BLOCKED;
    if (wb_social_are_friends(g, from, to)) return WB_SOCIAL_TRUST_FRIEND;
    /* a friend of `from` who is also a friend of `to` */
    for (uint32_t mid = 0; mid < g->n_users; mid++) {
        if (mid == from || mid == to) continue;
        if (wb_social_are_friends(g, from, mid) && wb_social_are_friends(g, mid, to))
            return WB_SOCIAL_TRUST_FOF;
    }
    return WB_SOCIAL_TRUST_STRANGER;
}

uint32_t wb_social_count(const wb_social *g) { return g ? g->n : 0; }

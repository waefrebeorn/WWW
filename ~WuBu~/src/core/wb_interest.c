/*
 * wb_interest.c -- parent-child interest propagation (LP-103 stone 1). See wb_interest.h.
 */
#include "wb_interest.h"

#include <stdio.h>
#include <string.h>

void wb_interest_init(wb_interest_set *s) {
    if (!s) return;
    s->n = 0;
}

uint32_t wb_interest_count(const wb_interest_set *s) {
    return s ? s->n : 0;
}

int wb_interest_add(wb_interest_set *s, const char *cell) {
    if (!s || !wb_qkey_valid(cell)) return -1;
    for (uint32_t i = 0; i < s->n; i++)
        if (strcmp(s->cells[i], cell) == 0) return 0;    /* a duplicate */
    if (s->n >= WB_INTEREST_MAX) return -1;
    snprintf(s->cells[s->n], WB_QKEY_MAX, "%s", cell);
    s->n++;
    return 0;
}

int wb_interest_covers(const wb_interest_set *s, const char *cell) {
    if (!s || !wb_qkey_valid(cell)) return 0;
    for (uint32_t i = 0; i < s->n; i++)
        if (wb_qkey_contains(s->cells[i], cell)) return 1;   /* member is cell or an ancestor */
    return 0;
}

int wb_interest_aggregate(const wb_interest_set *s, wb_interest_set *out) {
    if (!s || !out) return -1;
    wb_interest_init(out);
    for (uint32_t i = 0; i < s->n; i++) {
        if (wb_interest_add(out, s->cells[i]) != 0) return -1;
        /* walk to the root, adding every ancestor */
        char cur[WB_QKEY_MAX];
        snprintf(cur, sizeof cur, "%s", s->cells[i]);
        while (wb_qkey_level(cur) > 0) {
            char parent[WB_QKEY_MAX];
            if (wb_qkey_parent(cur, parent, sizeof parent) != 0) return -1;
            if (wb_interest_add(out, parent) != 0) return -1;
            memcpy(cur, parent, sizeof cur);
        }
    }
    return 0;
}

int wb_interest_refine(const wb_interest_set *s, wb_interest_set *out) {
    if (!s || !out) return -1;
    wb_interest_init(out);
    for (uint32_t i = 0; i < s->n; i++) {
        const char *c = s->cells[i];
        uint32_t level = wb_qkey_level(c);
        if (level + 1 >= WB_QKEY_MAX) continue;              /* cannot go deeper */
        /* append each of the four child digits */
        for (char d = '0'; d <= '3'; d++) {
            char child[WB_QKEY_MAX];
            size_t l = strlen(c);
            if (l + 1 >= sizeof child) return -1;
            memcpy(child, c, l);
            child[l] = d;
            child[l + 1] = '\0';
            if (wb_interest_add(out, child) != 0) return -1;
        }
    }
    return 0;
}

int wb_interest_coarsen(const wb_interest_set *s, uint32_t level, wb_interest_set *out) {
    if (!s || !out) return -1;
    wb_interest_init(out);
    for (uint32_t i = 0; i < s->n; i++) {
        const char *c = s->cells[i];
        uint32_t cl = wb_qkey_level(c);
        if (cl < level) continue;                            /* too shallow to have one */
        char cur[WB_QKEY_MAX];
        snprintf(cur, sizeof cur, "%s", c);
        while (wb_qkey_level(cur) > level) {
            char parent[WB_QKEY_MAX];
            if (wb_qkey_parent(cur, parent, sizeof parent) != 0) return -1;
            memcpy(cur, parent, sizeof cur);
        }
        if (wb_interest_add(out, cur) != 0) return -1;
    }
    return 0;
}

int wb_interest_equal(const wb_interest_set *a, const wb_interest_set *b) {
    if (!a || !b) return 0;
    if (a->n != b->n) return 0;
    /* order-sensitive: the sets are built in insertion order, so a mismatch in
     * order means the two servers computed them differently */
    for (uint32_t i = 0; i < a->n; i++)
        if (strcmp(a->cells[i], b->cells[i]) != 0) return 0;
    return 1;
}

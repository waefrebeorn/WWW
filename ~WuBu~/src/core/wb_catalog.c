/*
 * wb_catalog.c -- gametype catalog: search, charts, featured (LP-52 stone 1).
 */
#include "wb_catalog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int wb_catalog_init(wb_catalog *c, uint32_t cap) {
    if (!c || cap == 0) return -1;
    c->entries = (wb_catalog_entry *)calloc(cap, sizeof(wb_catalog_entry));
    if (!c->entries) return -1;
    c->n = 0;
    c->cap = cap;
    return 0;
}

void wb_catalog_free(wb_catalog *c) {
    if (!c) return;
    free(c->entries);
    c->entries = NULL;
    c->n = c->cap = 0;
}

void wb_catalog_clear(wb_catalog *c) {
    if (!c) return;
    c->n = 0;
}

const wb_catalog_entry *wb_catalog_find(const wb_catalog *c, uint32_t id) {
    if (!c || !c->entries) return NULL;
    for (uint32_t i = 0; i < c->n; i++) if (c->entries[i].id == id) return &c->entries[i];
    return NULL;
}

int wb_catalog_add(wb_catalog *c, const wb_catalog_entry *e) {
    if (!c || !c->entries || !e) return -1;
    if (wb_catalog_find(c, e->id)) return -1;
    if (c->n == c->cap) return -1;
    wb_catalog_entry *d = &c->entries[c->n++];
    memcpy(d, e, sizeof *d);
    return 0;
}

/* ---- case-insensitive substring search ----------------------------------- */

static int wb_catalog_ci_eq(char a, char b) {
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    return a == b;
}

static int wb_catalog_contains(const char *hay, const char *needle) {
    if (!hay || !needle) return 0;
    if (!*needle) return 1;
    for (const char *h = hay; *h; h++) {
        const char *a = h, *b = needle;
        while (*a && *b && wb_catalog_ci_eq(*a, *b)) { a++; b++; }
        if (!*b) return 1;
    }
    return 0;
}

int wb_catalog_matches(const wb_catalog_entry *e, const char *query) {
    if (!e) return 0;
    if (!query || !*query) return 1;
    if (wb_catalog_contains(e->name, query)) return 1;
    if (wb_catalog_contains(e->desc, query)) return 1;
    for (uint32_t i = 0; i < e->ntags; i++)
        if (wb_catalog_contains(e->tags[i], query)) return 1;
    return 0;
}

static int wb_catalog_has_tag(const wb_catalog_entry *e, const char *tag) {
    if (!e || !tag) return 0;
    for (uint32_t i = 0; i < e->ntags; i++)
        if (e->tags[i] && strcmp(e->tags[i], tag) == 0) return 1;
    return 0;
}

uint32_t wb_catalog_score(const wb_catalog_entry *e, const char *query) {
    if (!e) return 0;
    uint32_t s = e->players * 4u + e->rating * 10u;
    if (query && *query && wb_catalog_matches(e, query)) s += 100000u;   /* a match dominates */
    if (e->flags & WB_CATALOG_FEATURED) s += 50000u;
    return s;
}

/* ---- ordering ------------------------------------------------------------ */

/* Sort an id list by rank (score, then version, then id) without qsort_r, and
 * write at most `limit` of them. */
static void wb_catalog_sort_ids(const wb_catalog *c, const uint32_t *src, uint32_t n,
                                const char *query, uint32_t *out, uint32_t limit) {
    uint32_t *idx = (uint32_t *)malloc((size_t)n * sizeof(uint32_t));
    if (!idx) return;
    memcpy(idx, src, (size_t)n * sizeof(uint32_t));
    /* insertion sort: n is a catalog page, and the tie-break must be exact */
    for (uint32_t i = 1; i < n; i++) {
        uint32_t key = idx[i];
        uint32_t k = i;
        while (k > 0) {
            const wb_catalog_entry *a = wb_catalog_find(c, idx[k - 1]);
            const wb_catalog_entry *b = wb_catalog_find(c, key);
            uint32_t sa = wb_catalog_score(a, query), sb = wb_catalog_score(b, query);
            int after;
            if (sa != sb) after = sb > sa;
            else if (a->updated != b->updated) after = b->updated > a->updated;
            else after = key < idx[k - 1];
            if (!after) break;
            idx[k] = idx[k - 1];
            k--;
        }
        idx[k] = key;
    }
    uint32_t keep = n < limit ? n : limit;
    memcpy(out, idx, (size_t)keep * sizeof(uint32_t));
    free(idx);
}

static uint32_t wb_catalog_collect(const wb_catalog *c, const char *query, const char *tag,
                                   uint32_t *out, uint32_t cap) {
    uint32_t *hits = (uint32_t *)malloc((size_t)c->n * sizeof(uint32_t));
    if (!hits) return 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < c->n; i++) {
        const wb_catalog_entry *e = &c->entries[i];
        if (query && *query && !wb_catalog_matches(e, query)) continue;
        if (tag && *tag && !wb_catalog_has_tag(e, tag)) continue;
        hits[n++] = e->id;
    }
    uint32_t written = n < cap ? n : cap;
    /* sort EVERY hit, then truncate: sorting only the first `cap` would return
     * the wrong entries when the buffer is smaller than the result set */
    wb_catalog_sort_ids(c, hits, n, query, out, cap);
    free(hits);
    return written;
}

uint32_t wb_catalog_search(const wb_catalog *c, const char *query, const char *tag,
                           uint32_t *out, uint32_t cap) {
    if (!c || !c->entries || !out || cap == 0) return 0;
    return wb_catalog_collect(c, query, tag, out, cap);
}

uint32_t wb_catalog_charts(const wb_catalog *c, uint32_t *out, uint32_t cap) {
    if (!c || !c->entries || !out || cap == 0) return 0;
    /* charts ignore the query: rank by players, then rating, then id */
    uint32_t *idx = (uint32_t *)malloc((size_t)c->n * sizeof(uint32_t));
    if (!idx) return 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < c->n; i++) idx[n++] = c->entries[i].id;
    for (uint32_t i = 1; i < n; i++) {
        uint32_t key = idx[i], k = i;
        while (k > 0) {
            const wb_catalog_entry *a = wb_catalog_find(c, idx[k - 1]);
            const wb_catalog_entry *b = wb_catalog_find(c, key);
            int after;
            if (a->players != b->players) after = b->players > a->players;
            else if (a->rating != b->rating) after = b->rating > a->rating;
            else after = key < idx[k - 1];
            if (!after) break;
            idx[k] = idx[k - 1];
            k--;
        }
        idx[k] = key;
    }
    uint32_t written = n < cap ? n : cap;
    memcpy(out, idx, (size_t)written * sizeof(uint32_t));
    free(idx);
    return written;
}

uint32_t wb_catalog_featured(const wb_catalog *c, uint32_t *out, uint32_t cap) {
    if (!c || !c->entries || !out || cap == 0) return 0;
    uint32_t *idx = (uint32_t *)malloc((size_t)c->n * sizeof(uint32_t));
    if (!idx) return 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < c->n; i++)
        if (c->entries[i].flags & WB_CATALOG_FEATURED) idx[n++] = c->entries[i].id;
    /* ascending id, whatever order the entries were added in */
    for (uint32_t i = 1; i < n; i++) {
        uint32_t key = idx[i], k = i;
        while (k > 0 && idx[k - 1] > key) { idx[k] = idx[k - 1]; k--; }
        idx[k] = key;
    }
    uint32_t written = n < cap ? n : cap;
    memcpy(out, idx, (size_t)written * sizeof(uint32_t));
    free(idx);
    return written;
}

uint32_t wb_catalog_count(const wb_catalog *c) { return c ? c->n : 0; }

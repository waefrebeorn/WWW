/*
 * wb_catalog.h -- gametype catalog: search, charts, featured (LP-52 stone 1).
 *
 * How players find gametypes. A catalog is a set of entries -- a name, a
 * description, tags, a live player count, a rating, a version -- and three
 * questions asked of it:
 *
 *   search   which entries match this text (case-insensitive, over the name, the
 *            description and the tags), best first
 *   charts   the top entries by players, then rating, then id
 *   featured the entries a human put on the front page, in a stable order
 *
 * Ranking is a pure function of the entry and the query, so the same catalog
 * always produces the same order on every shard -- no "trending" jitter, no
 * pointer-keyed iteration, no tie left to chance.
 *
 * C11, no deps. Deterministic.
 */
#ifndef WB_CATALOG_H
#define WB_CATALOG_H

#include <stdint.h>

#define WB_CATALOG_MAX_TAGS 8
#define WB_CATALOG_NAME_LEN 64
#define WB_CATALOG_DESC_LEN 256

typedef enum {
    WB_CATALOG_FEATURED = 1u << 0
} wb_catalog_flag;

typedef struct {
    uint32_t id;
    char     name[WB_CATALOG_NAME_LEN];
    char     desc[WB_CATALOG_DESC_LEN];
    const char *tags[WB_CATALOG_MAX_TAGS];
    uint32_t ntags;
    uint32_t players;      /* a live player count */
    uint32_t rating;       /* 0..100 */
    uint32_t flags;
    uint64_t updated;      /* a version, used only for tie-breaking */
} wb_catalog_entry;

typedef struct {
    wb_catalog_entry *entries;
    uint32_t          n, cap;
} wb_catalog;

int  wb_catalog_init(wb_catalog *c, uint32_t cap);
void wb_catalog_free(wb_catalog *c);
void wb_catalog_clear(wb_catalog *c);

/* Add an entry (copied). Returns 0 on success, -1 if the id is taken or the
 * catalog is full. */
int  wb_catalog_add(wb_catalog *c, const wb_catalog_entry *e);

const wb_catalog_entry *wb_catalog_find(const wb_catalog *c, uint32_t id);

/* 1 if the entry's name, description or any tag contains `query`
 * case-insensitively. An empty query matches everything. */
int  wb_catalog_matches(const wb_catalog_entry *e, const char *query);

/* The rank score: a match dominates, then featured, then players and rating. */
uint32_t wb_catalog_score(const wb_catalog_entry *e, const char *query);

/* Search into `out` (cap entries), best first. `tag` is an optional exact
 * case-insensitive tag filter (NULL for none). Returns the count. */
uint32_t wb_catalog_search(const wb_catalog *c, const char *query, const char *tag,
                           uint32_t *out, uint32_t cap);

/* The charts: top entries by players, then rating, then id, into `out`. */
uint32_t wb_catalog_charts(const wb_catalog *c, uint32_t *out, uint32_t cap);

/* The featured entries, in ascending id order, into `out`. */
uint32_t wb_catalog_featured(const wb_catalog *c, uint32_t *out, uint32_t cap);

uint32_t wb_catalog_count(const wb_catalog *c);

#endif /* WB_CATALOG_H */

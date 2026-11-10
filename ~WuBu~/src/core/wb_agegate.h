/*
 * wb_agegate.h -- age gating (LP-54 stone 1).
 *
 * A rating is not a number: it is a tier in a region's ladder (ESRB in the US,
 * PEGI in the EU, CERO in Japan), and content descriptors can raise the floor
 * above the tier's own age. The same game is 10+ in one region and 16+ in
 * another, and the difference is data, not a branch in the product code.
 *
 * The effective minimum age is the LATER of the tier's own age and the highest
 * descriptor floor among the descriptors set -- never the sum, and never the
 * tier's age plus its own implied descriptors. Two descriptors do not compound:
 * "violence" (13) and "language" (10) together are 13, not 23. And a tier that
 * already implies violence is not pushed past the tier above it by listing that
 * descriptor again.
 *
 * C11, no deps. Deterministic.
 */
#ifndef WB_AGEGATE_H
#define WB_AGEGATE_H

#include <stdint.h>

/* Content descriptor bits. */
#define WB_AGEGATE_VIOLENCE  (1u << 0)
#define WB_AGEGATE_LANGUAGE  (1u << 1)
#define WB_AGEGATE_GAMBLING  (1u << 2)
#define WB_AGEGATE_HORROR    (1u << 3)
#define WB_AGEGATE_MAX_DESCRIPTORS 8

typedef struct {
    const char *name;      /* "Everyone", "Teen", ... */
    uint32_t    min_age;   /* the tier's base age */
    uint32_t    flags;     /* descriptors this tier already implies */
} wb_agegate_level;

typedef struct {
    uint32_t    bit;       /* a WB_AGEGATE_* flag */
    const char *code;      /* "violence", "language", ... */
    uint32_t    min_age;   /* the absolute floor this descriptor imposes */
} wb_agegate_descriptor;

typedef struct {
    const char *region;                          /* "US", "EU", "JP" */
    const wb_agegate_level *tiers;                /* ascending by min_age */
    uint32_t ntiers;
    const wb_agegate_descriptor *descs;          /* the region's descriptor table */
    uint32_t ndescs;
} wb_agegate_ladder;

typedef struct {
    uint32_t rating;      /* an index into the ladder's tiers */
    uint32_t flags;       /* content descriptor bits */
} wb_agegate_entry;

/* The tier for a rating index, or NULL if the index is out of range. */
const wb_agegate_level *wb_agegate_tier(const wb_agegate_ladder *ladder, uint32_t rating);

/* The effective minimum age: the later of the tier's base age and the highest
 * descriptor floor among the set bits. Returns 0 for an out-of-range rating. */
uint32_t wb_agegate_min_age(const wb_agegate_ladder *ladder, const wb_agegate_entry *e);

/* 1 if a user of `age` may access the entry; 0 if too young or the rating is out
 * of range. */
int  wb_agegate_allowed(const wb_agegate_ladder *ladder, const wb_agegate_entry *e, uint32_t age);

/* The descriptor codes set on an entry, into `out`. Returns the count. */
uint32_t wb_agegate_descriptors(const wb_agegate_ladder *ladder, const wb_agegate_entry *e,
                                const char **out, uint32_t cap);

#endif /* WB_AGEGATE_H */

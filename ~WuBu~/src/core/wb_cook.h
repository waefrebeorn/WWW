/*
 * wb_cook.h -- cook manifest (LP-46 stone 1).
 *
 * A cook produces a manifest: the assets in build order, each with the hash of
 * its cooked output. The order is part of the contract -- a dependent must appear
 * after everything it was built from -- and the chain over the whole manifest
 * makes a corrupted or reordered file detectable before it is deployed.
 *
 * This is distinct from wb_manifest, which records the hash of files already on
 * disk; this one records the output of the cook itself.
 *
 * C11, no deps.
 */
#ifndef WB_COOK_H
#define WB_COOK_H

#include <stdint.h>
#include "wb_assetgraph.h"

typedef struct {
    uint32_t asset;
    uint64_t hash;      /* the hash of the cooked output */
} wb_cook_entry;

typedef struct {
    wb_cook_entry *entries;
    uint32_t       n, cap;
    uint64_t       chain;   /* a chain over every (asset, hash) in order */
} wb_cook_manifest;

int  wb_cook_init(wb_cook_manifest *m, uint32_t cap);
void wb_cook_free(wb_cook_manifest *m);
void wb_cook_clear(wb_cook_manifest *m);

/* Append an entry. Returns 0 on success, -1 if the manifest is full. */
int  wb_cook_add(wb_cook_manifest *m, uint32_t asset, uint64_t hash);

/* Recompute the chain over the stored entries and store it. Returns the chain. */
uint64_t wb_cook_recompute(wb_cook_manifest *m);
/* 1 if the stored chain matches a recomputation. */
int  wb_cook_verify(const wb_cook_manifest *m);

/* The first slot where the two manifests differ, or n if they are identical.
 * Two manifests of different lengths differ at the first extra slot. */
uint32_t wb_cook_diff(const wb_cook_manifest *a, const wb_cook_manifest *b);

/* 1 if every asset in the graph appears exactly once and every dependency is
 * earlier in the manifest than the asset that needs it. */
int  wb_cook_order_ok(const wb_cook_manifest *m, const wb_assetgraph *g);

#endif /* WB_COOK_H */

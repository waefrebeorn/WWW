/*
 * wb_geolic.h -- per-cell open-data provenance and licensing (LP-110 stone 1).
 *
 * LP-79 gave every ASSET a license. Geo data needs more than that: a planet is
 * assembled from datasets with different terms -- ODbL for OSM, CDLA-Permissive
 * for the Microsoft Building Footprints, public domain for the USGS Mars DEMs --
 * and the terms attach to the CELLS the data came from, not to a file. A world
 * that ingests them has to know which cells are share-alike before it derives
 * anything from them.
 *
 * So a license is recorded against a CELL, and a record covers the cell and every
 * descendant of it. The license that GOVERNS a cell is the most restrictive one
 * among the cell and its ancestors:
 *
 *   - a share-alike grant cannot be washed by a finer public-domain grant, so a
 *     cell under an ODbL region stays ODbL no matter what is granted below it;
 *   - a public-domain region can be made more restrictive from below, so a cell
 *     inside an ODbL enclave is ODbL even though the planet around it is free.
 *
 * The same rule answers what a DERIVED cell carries: fold the source licenses and
 * the result is the most restrictive of them. That is the field the DDC must
 * store alongside the content hash, so a derived asset's license travels with its
 * bytes rather than being re-decided later.
 *
 * C11, no deps beyond the quadkey in wb_qkey. Deterministic.
 */
#ifndef WB_GEOLIC_H
#define WB_GEOLIC_H

#include <stddef.h>
#include <stdint.h>

#include "wb_qkey.h"

#define WB_GEOLIC_MAX 32
#define WB_GEOLIC_NAME 64

typedef enum {
    WB_GEOLIC_PUBLIC_DOMAIN = 0,    /* no restriction */
    WB_GEOLIC_CDLA_PERMISSIVE = 1,  /* permissive, attribution required */
    WB_GEOLIC_ODBL = 2,             /* share-alike */
    WB_GEOLIC_PROPRIETARY = 3       /* not redistributable */
} wb_geolic_license;

typedef struct {
    char     cell[WB_QKEY_MAX];
    char     dataset[WB_GEOLIC_NAME];
    uint32_t license;               /* a wb_geolic_license */
} wb_geolic_rec;

typedef struct {
    wb_geolic_rec recs[WB_GEOLIC_MAX];
    uint32_t      n;
} wb_geolic_ledger;

void wb_geolic_init(wb_geolic_ledger *l);
uint32_t wb_geolic_count(const wb_geolic_ledger *l);

/* Record the license of the dataset a cell came from. The record covers the cell
 * and every descendant. A cell may carry more than one record when it draws from
 * more than one dataset; the governing license is the most restrictive of them.
 * Returns 0 on success, -1 if refused. */
int  wb_geolic_set(wb_geolic_ledger *l, const char *cell, const char *dataset,
                   wb_geolic_license license);

const wb_geolic_rec *wb_geolic_find(const wb_geolic_ledger *l, const char *cell);

/* The license that GOVERNS `cell`: the most restrictive among the cell and its
 * ancestors, with the dataset that supplied it. Returns 0 on success, -1 if the
 * cell is invalid or no record covers it -- an unlicensed cell. */
int  wb_geolic_effective(const wb_geolic_ledger *l, const char *cell,
                         wb_geolic_rec *out);

/* 1 if some record covers `cell`. */
int  wb_geolic_covers(const wb_geolic_ledger *l, const char *cell);

/* The license a derivative of these licenses must carry: the most restrictive.
 * `n` 0 returns WB_GEOLIC_PUBLIC_DOMAIN -- nothing restricted. */
wb_geolic_license wb_geolic_fold(const wb_geolic_license *lics, uint32_t n);
wb_geolic_license wb_geolic_combine(wb_geolic_license a, wb_geolic_license b);

int wb_geolic_can_redistribute(wb_geolic_license l);
int wb_geolic_share_alike(wb_geolic_license l);
const char *wb_geolic_name(wb_geolic_license l);

#endif /* WB_GEOLIC_H */

/*
 * wb_placeval.h -- AGI-authored place validation (LP-112 stone 1).
 *
 * An AGI-authored place is published into a world that other people's work and
 * other people's rights already occupy. Nothing checked it before publish: not
 * the elevation it claimed against the source it came from, not whether it sat
 * inside the volume it claimed to sit inside, not whether it collided with a
 * volume already reserved, and not whether every cell in it was licensed at all.
 *
 * So a place is validated against four things before it goes live:
 *
 *   STRUCTURE.    it has cells, they are valid quadkeys, they are all at one
 *                 level, and they form ONE connected region -- a place is a
 *                 region, not a bag of cells.
 *   CONTAINMENT.  every cell is contained by the parent volume the place declares.
 *   SOURCE.       every elevation sample the place claims agrees with the source
 *                 DEM for that cell, and a cell the source does not cover cannot
 *                 be given an invented elevation.
 *   RIGHTS.       no cell is already reserved, every cell has a governing license,
 *                 and the license the place DECLARES is at least as restrictive as
 *                 the most restrictive license it draws on -- a place cannot be
 *                 published more freely than the data inside it.
 *
 * A check that cannot be made is skipped rather than assumed passed: a NULL
 * source DEM, a NULL license ledger, or an empty reserved list means that
 * constraint is not being applied by this caller.
 *
 * C11, no deps beyond the quadkey in wb_qkey and the licenses in wb_geolic.
 * Deterministic: the same place and context always produce the same verdict.
 */
#ifndef WB_PLACEVAL_H
#define WB_PLACEVAL_H

#include <stdint.h>

#include "wb_geolic.h"
#include "wb_qkey.h"

#define WB_PLACEVAL_MAX_CELLS 32
#define WB_PLACEVAL_MAX_SAMPLES 32
#define WB_PLACEVAL_MAX_RESERVED 32
#define WB_PLACEVAL_NAME 64

typedef enum {
    WB_PLACEVAL_OK = 0,
    WB_PLACEVAL_ERR_EMPTY,          /* the place has no cells */
    WB_PLACEVAL_ERR_BAD_CELL,       /* a cell is not a valid quadkey */
    WB_PLACEVAL_ERR_MIXED_DEPTH,    /* the cells are at more than one level */
    WB_PLACEVAL_ERR_DISCONNECTED,   /* the cells do not form one region */
    WB_PLACEVAL_ERR_NOT_CONTAINED,  /* a cell is outside the declared parent */
    WB_PLACEVAL_ERR_DEM_MISMATCH,   /* a sample disagrees with the source DEM */
    WB_PLACEVAL_ERR_RESERVED,       /* a cell is already reserved */
    WB_PLACEVAL_ERR_UNLICENSED,     /* a cell has no governing license */
    WB_PLACEVAL_ERR_LICENSE_WEAK    /* the declared license is weaker than the sources */
} wb_placeval_reason;

/* One elevation the place claims for one cell, in centimetres. */
typedef struct {
    char     cell[WB_QKEY_MAX];
    int32_t  elevation;
} wb_placeval_sample;

typedef struct {
    char     name[WB_PLACEVAL_NAME];
    uint32_t license;               /* a wb_geolic_license, the declared one */
    char     parent[WB_QKEY_MAX];   /* the volume the place must sit inside */
    char     cells[WB_PLACEVAL_MAX_CELLS][WB_QKEY_MAX];
    uint32_t n;
    wb_placeval_sample samples[WB_PLACEVAL_MAX_SAMPLES];
    uint32_t nsamples;
} wb_place;

typedef struct {
    const wb_placeval_sample *dem;   /* the source elevation model, or NULL */
    uint32_t   ndem;
    char       reserved[WB_PLACEVAL_MAX_RESERVED][WB_QKEY_MAX];
    uint32_t   nreserved;
    const wb_geolic_ledger *lic;     /* the license ledger, or NULL */
} wb_placeval_ctx;

/* Validate a place. Returns 0 if it may be published, -1 if it may not, with
 * `reason` receiving a wb_placeval_reason. */
int wb_placeval_check(const wb_place *place, const wb_placeval_ctx *ctx,
                      uint32_t *reason);

const char *wb_placeval_reason_name(uint32_t reason);

#endif /* WB_PLACEVAL_H */

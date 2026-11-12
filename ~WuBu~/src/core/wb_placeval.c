/*
 * wb_placeval.c -- AGI-authored place validation (LP-112 stone 1). See wb_placeval.h.
 */
#include "wb_placeval.h"

#include <string.h>

/* The four edge-neighbours of a cell at its own level. Returns how many were
 * written (0..4). */
static int wb_placeval_neighbours(const char *cell, char out[4][WB_QKEY_MAX]) {
    uint32_t level, col, row;
    if (wb_qkey_decode(cell, &level, &col, &row) != 0) return 0;
    uint64_t limit = 1ull << level;
    int n = 0;
    if (col > 0)                 wb_qkey_encode(level, col - 1, row, out[n++], WB_QKEY_MAX);
    if ((uint64_t)col + 1 < limit) wb_qkey_encode(level, col + 1, row, out[n++], WB_QKEY_MAX);
    if (row > 0)                 wb_qkey_encode(level, col, row - 1, out[n++], WB_QKEY_MAX);
    if ((uint64_t)row + 1 < limit) wb_qkey_encode(level, col, row + 1, out[n++], WB_QKEY_MAX);
    return n;
}

/* 1 if every cell is reachable from the first by edge-adjacent steps. */
static int wb_placeval_connected(const wb_place *pl) {
    if (pl->n <= 1) return 1;
    uint32_t seen[WB_PLACEVAL_MAX_CELLS];
    uint32_t queue[WB_PLACEVAL_MAX_CELLS];
    uint32_t nseen = 0, qhead = 0, qtail = 0;

    seen[nseen++] = 0;
    queue[qtail++] = 0;
    while (qhead < qtail) {
        char nb[4][WB_QKEY_MAX];
        int nn = wb_placeval_neighbours(pl->cells[queue[qhead++]], nb);
        for (int k = 0; k < nn; k++) {
            for (uint32_t i = 0; i < pl->n; i++) {
                if (strcmp(pl->cells[i], nb[k]) != 0) continue;
                uint32_t s = 0;
                while (s < nseen && seen[s] != i) s++;
                if (s == nseen) {
                    seen[nseen++] = i;
                    queue[qtail++] = i;
                }
                break;
            }
        }
    }
    return nseen == pl->n;
}

static int wb_placeval_is_reserved(const wb_placeval_ctx *ctx, const char *cell) {
    if (!ctx || ctx->nreserved == 0) return 0;
    for (uint32_t i = 0; i < ctx->nreserved; i++)
        if (strcmp(ctx->reserved[i], cell) == 0) return 1;
    return 0;
}

/* The elevation the source DEM gives a cell, or 0 if it covers it. Returns 0 if
 * the DEM has no sample for the cell. */
static int wb_placeval_dem_elevation(const wb_placeval_ctx *ctx, const char *cell,
                                     int32_t *out) {
    if (!ctx || !ctx->dem) return 0;
    for (uint32_t i = 0; i < ctx->ndem; i++) {
        if (strcmp(ctx->dem[i].cell, cell) == 0) {
            if (out) *out = ctx->dem[i].elevation;
            return 1;
        }
    }
    return 0;
}

int wb_placeval_check(const wb_place *pl, const wb_placeval_ctx *ctx, uint32_t *reason) {
    if (reason) *reason = WB_PLACEVAL_OK;
    if (!pl) {
        if (reason) *reason = WB_PLACEVAL_ERR_EMPTY;
        return -1;
    }

    /* ---- structure ---- */
    if (pl->n == 0) {
        if (reason) *reason = WB_PLACEVAL_ERR_EMPTY;
        return -1;
    }
    for (uint32_t i = 0; i < pl->n; i++) {
        if (!wb_qkey_valid(pl->cells[i])) {
            if (reason) *reason = WB_PLACEVAL_ERR_BAD_CELL;
            return -1;
        }
    }
    uint32_t depth = wb_qkey_depth(pl->cells[0]);
    for (uint32_t i = 1; i < pl->n; i++) {
        if (wb_qkey_depth(pl->cells[i]) != depth) {
            if (reason) *reason = WB_PLACEVAL_ERR_MIXED_DEPTH;
            return -1;
        }
    }
    if (!wb_placeval_connected(pl)) {
        if (reason) *reason = WB_PLACEVAL_ERR_DISCONNECTED;
        return -1;
    }

    /* ---- containment ---- */
    if (!wb_qkey_valid(pl->parent)) {
        if (reason) *reason = WB_PLACEVAL_ERR_NOT_CONTAINED;
        return -1;
    }
    for (uint32_t i = 0; i < pl->n; i++) {
        if (!wb_qkey_contains(pl->parent, pl->cells[i])) {
            if (reason) *reason = WB_PLACEVAL_ERR_NOT_CONTAINED;
            return -1;
        }
    }

    /* ---- source DEM (skipped when the caller supplies no source) ---- */
    if (ctx && ctx->dem) {
        for (uint32_t i = 0; i < pl->nsamples; i++) {
            int32_t src = 0;
            if (!wb_placeval_dem_elevation(ctx, pl->samples[i].cell, &src) ||
                src != pl->samples[i].elevation) {
                if (reason) *reason = WB_PLACEVAL_ERR_DEM_MISMATCH;
                return -1;
            }
        }
    }

    /* ---- reservation ---- */
    for (uint32_t i = 0; i < pl->n; i++) {
        if (wb_placeval_is_reserved(ctx, pl->cells[i])) {
            if (reason) *reason = WB_PLACEVAL_ERR_RESERVED;
            return -1;
        }
    }

    /* ---- rights ---- */
    wb_geolic_license worst = WB_GEOLIC_PUBLIC_DOMAIN;
    if (ctx && ctx->lic) {
        for (uint32_t i = 0; i < pl->n; i++) {
            wb_geolic_rec rec;
            if (wb_geolic_effective(ctx->lic, pl->cells[i], &rec) != 0) {
                if (reason) *reason = WB_PLACEVAL_ERR_UNLICENSED;
                return -1;
            }
            worst = wb_geolic_fold((const wb_geolic_license[]){worst, rec.license}, 2);
        }
        if ((unsigned)pl->license < (unsigned)worst) {
            if (reason) *reason = WB_PLACEVAL_ERR_LICENSE_WEAK;
            return -1;
        }
    }
    return 0;
}

const char *wb_placeval_reason_name(uint32_t reason) {
    switch (reason) {
    case WB_PLACEVAL_OK:              return "ok";
    case WB_PLACEVAL_ERR_EMPTY:        return "the place has no cells";
    case WB_PLACEVAL_ERR_BAD_CELL:     return "a cell is not a valid quadkey";
    case WB_PLACEVAL_ERR_MIXED_DEPTH:  return "the cells are at more than one level";
    case WB_PLACEVAL_ERR_DISCONNECTED: return "the cells do not form one region";
    case WB_PLACEVAL_ERR_NOT_CONTAINED: return "a cell is outside the declared parent";
    case WB_PLACEVAL_ERR_DEM_MISMATCH: return "a sample disagrees with the source DEM";
    case WB_PLACEVAL_ERR_RESERVED:     return "a cell is already reserved";
    case WB_PLACEVAL_ERR_UNLICENSED:   return "a cell has no governing license";
    case WB_PLACEVAL_ERR_LICENSE_WEAK: return "the declared license is weaker than the sources";
    default:                           return "?";
    }
}

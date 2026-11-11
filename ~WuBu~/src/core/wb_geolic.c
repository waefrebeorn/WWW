/*
 * wb_geolic.c -- per-cell open-data provenance and licensing (LP-110 stone 1).
 * See wb_geolic.h.
 */
#include "wb_geolic.h"

#include <string.h>

static int wb_geolic_rank(uint32_t license) {
    /* The enum is already ordered from least to most restrictive. */
    return (int)license;
}

void wb_geolic_init(wb_geolic_ledger *l) {
    if (!l) return;
    memset(l, 0, sizeof *l);
}

uint32_t wb_geolic_count(const wb_geolic_ledger *l) {
    return l ? l->n : 0;
}

int wb_geolic_set(wb_geolic_ledger *l, const char *cell, const char *dataset,
                  wb_geolic_license license) {
    if (!l || !cell || !wb_qkey_valid(cell)) return -1;
    if (!dataset || !*dataset) return -1;
    if (strlen(dataset) >= WB_GEOLIC_NAME) return -1;
    if ((unsigned)license > (unsigned)WB_GEOLIC_PROPRIETARY) return -1;
    if (l->n >= WB_GEOLIC_MAX) return -1;

    wb_geolic_rec *r = &l->recs[l->n++];
    memset(r, 0, sizeof *r);
    strncpy(r->cell, cell, WB_QKEY_MAX - 1);
    strncpy(r->dataset, dataset, WB_GEOLIC_NAME - 1);
    r->license = license;
    return 0;
}

const wb_geolic_rec *wb_geolic_find(const wb_geolic_ledger *l, const char *cell) {
    if (!l || !cell) return NULL;
    for (uint32_t i = 0; i < l->n; i++)
        if (strcmp(l->recs[i].cell, cell) == 0) return &l->recs[i];
    return NULL;
}

int wb_geolic_covers(const wb_geolic_ledger *l, const char *cell) {
    if (!l || !cell || !wb_qkey_valid(cell)) return 0;
    for (uint32_t i = 0; i < l->n; i++)
        if (wb_qkey_contains(l->recs[i].cell, cell)) return 1;
    return 0;
}

int wb_geolic_effective(const wb_geolic_ledger *l, const char *cell,
                        wb_geolic_rec *out) {
    if (out) memset(out, 0, sizeof *out);
    if (!l || !cell || !wb_qkey_valid(cell)) return -1;

    const wb_geolic_rec *best = NULL;
    for (uint32_t i = 0; i < l->n; i++) {
        if (!wb_qkey_contains(l->recs[i].cell, cell)) continue;
        if (!best ||
            wb_geolic_rank(l->recs[i].license) > wb_geolic_rank(best->license))
            best = &l->recs[i];
    }
    if (!best) return -1;                  /* an unlicensed cell */
    if (out) *out = *best;
    return 0;
}

wb_geolic_license wb_geolic_combine(wb_geolic_license a, wb_geolic_license b) {
    return wb_geolic_rank(a) >= wb_geolic_rank(b) ? a : b;
}

wb_geolic_license wb_geolic_fold(const wb_geolic_license *lics, uint32_t n) {
    wb_geolic_license acc = WB_GEOLIC_PUBLIC_DOMAIN;
    if (!lics) return acc;
    for (uint32_t i = 0; i < n; i++)
        acc = wb_geolic_combine(acc, lics[i]);
    return acc;
}

int wb_geolic_can_redistribute(wb_geolic_license l) {
    return l != WB_GEOLIC_PROPRIETARY;
}

int wb_geolic_share_alike(wb_geolic_license l) {
    return l == WB_GEOLIC_ODBL;
}

const char *wb_geolic_name(wb_geolic_license l) {
    switch (l) {
    case WB_GEOLIC_PUBLIC_DOMAIN:   return "public-domain";
    case WB_GEOLIC_CDLA_PERMISSIVE: return "CDLA-Permissive-2.0";
    case WB_GEOLIC_ODBL:            return "ODbL-1.0";
    case WB_GEOLIC_PROPRIETARY:     return "proprietary";
    default:                        return "?";
    }
}

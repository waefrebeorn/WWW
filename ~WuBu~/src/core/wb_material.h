/*
 * wb_material.h -- material draw sorting (LP-12 stone 1).
 *
 * A material is a shader plus a parameter block. The CPU cost of a draw is
 * dominated by state changes, so the first job of a material system is to order
 * the draws: opaque geometry grouped by (shader, params) so consecutive draws
 * share state, and alpha-blended geometry within the same state sorted
 * back-to-front so the blend is correct.
 *
 * The sort is stable -- a draw's original index breaks ties -- so the result is
 * deterministic for a given input order.
 *
 * C11, no deps.
 */
#ifndef WB_MATERIAL_H
#define WB_MATERIAL_H

#include <stdint.h>

typedef struct {
    uint32_t shader;
    uint32_t params;   /* a bindless index to the parameter block */
    float    depth;    /* camera-space depth; larger is farther */
    uint32_t order;    /* the original index, for a stable tie-break */
} wb_draw;

/* Sort draws in place. `blended` selects back-to-front ordering by depth within
 * a (shader, params) group; otherwise draws are front-to-back. */
void wb_material_sort(wb_draw *draws, uint32_t n, int blended);

#endif /* WB_MATERIAL_H */

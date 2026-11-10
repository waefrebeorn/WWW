/*
 * wb_stream.c -- streaming budget selection (LP-13 stone 1). See wb_stream.h.
 */
#include "wb_stream.h"

#include <stdlib.h>

uint32_t wb_stream_select(const wb_stream_req *reqs, uint32_t n,
                          uint32_t budget, uint32_t *out_indices) {
    if (!reqs || !out_indices || n == 0 || budget == 0) return 0;

    unsigned char *taken = (unsigned char *)calloc(n, 1);
    if (!taken) return 0;

    uint32_t spent = 0, selected = 0;
    for (;;) {
        /* pick the highest-priority untaken request, ties by index */
        uint32_t best = (uint32_t)-1;
        float best_p = -1.0f;
        for (uint32_t i = 0; i < n; i++) {
            if (taken[i]) continue;
            if (reqs[i].priority > best_p) { best_p = reqs[i].priority; best = i; }
        }
        if (best == (uint32_t)-1) break;
        taken[best] = 1;
        if (reqs[best].bytes > budget - spent) continue;   /* does not fit: skip */
        spent += reqs[best].bytes;
        out_indices[selected++] = best;
        if (spent == budget) break;
    }

    free(taken);
    return selected;
}

/*
 * wb_snap.c -- per-tick world snapshot + digest (LP-03 stone 3). See wb_snap.h.
 */
#include "wb_snap.h"
#include "wb_det.h"

#include <stdlib.h>

struct wb_snap { uint64_t digest; };

wb_snap *wb_snap_capture(wb_world *w, const wb_comp_type *types, int ntypes) {
    if (!w || !types || ntypes < 0) return NULL;
    wb_snap *s = (wb_snap *)calloc(1, sizeof(*s));
    if (!s) return NULL;

    uint64_t h = WB_DET_FNV_OFFSET;
    uint32_t ni = wb_world_next_index(w);
    for (uint32_t i = 0; i < ni; i++) {
        if (!wb_world_slot_alive(w, i)) continue;
        uint32_t gen = wb_world_slot_gen(w, i);
        h = wb_det_checksum_stream(h, &gen, sizeof(gen));
        for (int t = 0; t < ntypes; t++) {
            wb_comp_type c = types[t];
            if (!wb_world_comp_present(w, c, i)) continue;
            size_t sz = wb_world_comp_elem_size(w, c);
            const unsigned char *base = (const unsigned char *)wb_world_comp_data(w, c);
            h = wb_det_checksum_stream(h, base + (size_t)i * sz, sz);
        }
    }
    s->digest = h;
    return s;
}

void wb_snap_free(wb_snap *s) { free(s); }

uint64_t wb_snap_digest(const wb_snap *s) { return s ? s->digest : 0; }
int      wb_snap_equal(const wb_snap *a, const wb_snap *b) {
    return a && b ? (a->digest == b->digest) : (a == b);
}

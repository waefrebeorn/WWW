/*
 * wb_geo_demo.c -- Slice 5: wire the engine together. A Lua-driven "camera"
 * flies over the planet; each frame it requests the tile under a focus
 * quadkey (plus a small ring around it) from a synthetic geodata source.
 * The C11 tile store pages tiles in from the source and evicts LRU under a
 * seat budget -- the ambient-culling loop, for real. State is printed so
 * the resident/evicted behavior is visible, not just asserted.
 *
 * Also proves the full pipeline: qkey <- lon/lat (camera pos) <- tile store
 * <- gluing callbacks.
 */
#include "wb_spatial.h"
#include "wb_tiles.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* --- synthetic geodata: a deterministic 1-byte "elevation" per tile.
 *      Reading it is the stand-in for pulling real DEM/satellite bytes. --- */
typedef struct { uint32_t loads; uint64_t bytes; } geo_src;

static int geo_load(void *user, wb_qkey key, uint8_t plane,
                    void *out, size_t out_len) {
    geo_src *g = (geo_src *)user;
    (void)plane;
    /* a fake "terrain byte": derived from the key so it's stable+verifiable */
    uint8_t v = (uint8_t)((wb_qkey_tx(key) * 31 + wb_qkey_ty(key) * 17) & 0xFF);
    memset(out, v, out_len);
    g->loads++;
    g->bytes += out_len;
    return 0;
}

/* ring: request the tiles on a 3x3 square around the focus (1 km-ish cell). */
static void request_ring(wb_tile_store *s, wb_qkey focus, geo_src *g) {
    uint32_t lv = wb_qkey_level(focus);
    uint32_t tx = wb_qkey_tx(focus), ty = wb_qkey_ty(focus);
    uint32_t side = 1u << lv;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int32_t nx = (int32_t)tx + dx, ny = (int32_t)ty + dy;
            if (nx < 0 || ny < 0 || nx >= (int32_t)side || ny >= (int32_t)side)
                continue;
            wb_qkey k = wb_qkey_make(lv, (uint32_t)nx, (uint32_t)ny);
            wb_tiles_request(s, k, 0, geo_load, g);
        }
    }
}

/* report the resident set for the current frame (what ambient culling sees) */
static void dump_resident(wb_tile_store *s, wb_qkey focus) {
    uint32_t lv = wb_qkey_level(focus);
    uint32_t tx = wb_qkey_tx(focus), ty = wb_qkey_ty(focus);
    uint32_t side = 1u << lv;
    int residents = 0;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            int32_t nx = (int32_t)tx + dx, ny = (int32_t)ty + dy;
            if (nx < 0 || ny < 0 || nx >= (int32_t)side || ny >= (int32_t)side)
                continue;
            if (wb_tiles_resident(s) == 0) continue;
            if (wb_tiles_get(s, wb_qkey_make(lv, (uint32_t)nx, (uint32_t)ny), 0))
                residents++;
        }
    }
    printf("  resident in 5x5 ring: %d  (store resident=%u)\n",
           residents, wb_tiles_resident(s));
}

int main(void) {
    wb_tile_store_cfg cfg = { .payload_size = 4, .max_resident = 9, .planes = 1 };
    wb_tile_store *s = wb_tiles_create(&cfg);
    geo_src g = {0, 0};

    /* "fly" diagonally across a slice of the planet at zoom level 8, moving
     * roughly 2 tiles/frame so LRU eviction is exercised by the ring. */
    const uint32_t LV = 8;
    uint32_t tx0 = 40, ty0 = 40;   /* start focus cell */
    printf("=== flying over level-%u tile grid; camera at 3x3 ring ===\n", LV);

    for (int frame = 0; frame < 8; frame++) {
        uint32_t tx = tx0 + frame * 2;
        uint32_t ty = ty0 + frame * 2;
        wb_qkey focus = wb_qkey_make(LV, tx, ty);
        request_ring(s, focus, &g);

        /* quiesce: give the store one burst per frame so resident policy is
         * consistent with the printed ring */
        if (frame % 2 == 0) {
            printf("frame %d focus=(%u,%u) lvl8:", frame, tx, ty);
            dump_resident(s, focus);
        }
    }

    printf("\ntotal loads: %u  bytes served: %llu\n",
           g.loads, (unsigned long long)g.bytes);
    printf("=== demo complete ===\n");
    wb_tiles_destroy(s);
    return 0;
}

/*
 * wb_tiles_test.c -- verification of the quadkey tile store.
 *
 * Real geodata-style loads: a synthetic "height source" that returns a
 * deterministic byte per (key,plane). Verifies demand-paging, the resident
 * seat budget + LRU eviction, the get-path, error propagation, and evict.
 */
#include "wb_tiles.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) g_pass++; \
    else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } \
} while (0)

/* --- synthetic geodata source: each tile holds 1 byte = its tile's tx. --- */
typedef struct { uint32_t next_error; } src_ctx;

static int fake_height(void *user, wb_qkey key, uint8_t plane,
                       void *out, size_t out_len) {
    (void)plane;
    src_ctx *src = (src_ctx *)user;
    if (src->next_error) { src->next_error--; return 1; }
    uint8_t v = (uint8_t)wb_qkey_tx(key);   /* byte reflects tx, verifiable */
    memset(out, v, out_len);
    return 0;
}

int main(void) {
    wb_tile_store_cfg cfg = { .payload_size = 1, .max_resident = 4, .planes = 1 };
    wb_tile_store *s = wb_tiles_create(&cfg);
    CHECK(s != NULL, "store created");

    /* empty store */
    CHECK(wb_tiles_resident(s) == 0, "starts empty");
    wb_qkey k0 = wb_qkey_make(2, 1, 0);
    CHECK(wb_tiles_get(s, k0, 0) == NULL, "unrequested tile not resident");

    /* demand-load 2 tiles */
    src_ctx src = {0};
    int r1 = wb_tiles_request(s, k0, 0, fake_height, &src);
    CHECK(r1 == 1, "first request loads + becomes resident");

    wb_qkey k1 = wb_qkey_make(2, 2, 0);
    int r2 = wb_tiles_request(s, k1, 0, fake_height, &src);
    CHECK(r2 == 1, "second request loads");
    CHECK(wb_tiles_resident(s) == 2, "2 resident");

    /* get returns the loaded payload (byte == tx) */
    uint8_t *p0 = wb_tiles_get(s, k0, 0);
    CHECK(p0 && *p0 == 1, "get returns loaded payload for tx=1");
    uint8_t *p1 = wb_tiles_get(s, k1, 0);
    CHECK(p1 && *p1 == 2, "get returns payload for tx=2");

    /* re-request returns immediately (already resident, no double-load) */
    src.next_error = 999; /* would fail if re-loaded */
    int r3 = wb_tiles_request(s, k0, 0, fake_height, &src);
    CHECK(r3 == 1, "re-request of resident tile is a hit, no reload");

    /* --- LRU eviction (dedicated store, clean request order) --- */
    src.next_error = 0;   /* the 999 above must not leak into these loads */
    wb_tile_store_cfg lru_cfg = { .payload_size = 1, .max_resident = 3, .planes = 1 };
    wb_tile_store *ls = wb_tiles_create(&lru_cfg);
    wb_qkey l0 = wb_qkey_make(2, 100, 0); /* oldest */
    wb_qkey l1 = wb_qkey_make(2, 101, 0);
    wb_qkey l2 = wb_qkey_make(2, 102, 0);
    wb_qkey l3 = wb_qkey_make(2, 103, 0);
    wb_tiles_request(ls, l0, 0, fake_height, &src);
    wb_tiles_request(ls, l1, 0, fake_height, &src);
    wb_tiles_request(ls, l2, 0, fake_height, &src);   /* 3 of 3 seats */
    wb_tiles_request(ls, l3, 0, fake_height, &src);   /* full -> evict l0 */
    CHECK(wb_tiles_resident(ls) == 3, "LRU store resident capped at 3");
    CHECK(wb_tiles_get(ls, l0, 0) == NULL, "oldest (l0) evicted under LRU");
    CHECK(wb_tiles_get(ls, l1, 0) != NULL, "l1 still resident");
    CHECK(wb_tiles_get(ls, l3, 0) != NULL, "newest (l3) resident");
    wb_tiles_destroy(ls);

    /* a previously-evicted tile can be re-requested (re-paginated in) */
    src.next_error = 0;
    int rr = wb_tiles_request(s, k0, 0, fake_height, &src);
    CHECK(rr == 1, "re-paging an evicted tile works");

    /* error propagation: loader fails -> not resident, request returns 0 */
    wb_tile_store_cfg ecfg = { .payload_size = 1, .max_resident = 2, .planes = 1 };
    wb_tile_store *es = wb_tiles_create(&ecfg);
    src_ctx esrc = { .next_error = 1 };
    wb_qkey ek = wb_qkey_make(3, 5, 5);
    int er = wb_tiles_request(es, ek, 0, fake_height, &esrc);
    CHECK(er == 0, "failed load never becomes resident");
    CHECK(wb_tiles_resident(es) == 0, "error tile not counted resident");
    CHECK(wb_tiles_get(es, ek, 0) == NULL, "error tile has no payload");
    wb_tiles_destroy(es);

    /* ------------------------------------------------------------------ */
    /* REGRESSION: sustained eviction across many frames. Multiple evictions
     * accumulate tombstones; evict_oldest scans them. Without a tombstone
     * guard this dereferenced the sentinel and segfaulted (caught in the
     * geo demo). Drive a 3x3 "camera ring" across the map for many frames
     * over a hard-seated store -- must stay resident-capped and live. */
    {
        wb_tile_store_cfg scfg = { .payload_size = 4, .max_resident = 9, .planes = 1 };
        wb_tile_store *big = wb_tiles_create(&scfg);
        src_ctx s2 = {0};
        const uint32_t LV = 8, tx0 = 40, ty0 = 40;
        int frames = 30;
        for (int fr = 0; fr < frames; fr++) {
            uint32_t tx = tx0 + fr * 2, ty = ty0 + fr * 2;
            uint32_t side = 1u << LV;
            for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int32_t nx = (int32_t)tx + dx, ny = (int32_t)ty + dy;
                if (nx < 0 || ny < 0 || nx >= (int32_t)side || ny >= (int32_t)side) continue;
                wb_tiles_request(big, wb_qkey_make(LV,(uint32_t)nx,(uint32_t)ny), 0, fake_height, &s2);
            }
            /* after every frame the seat budget must hold */
            if (wb_tiles_resident(big) > scfg.max_resident) {
                g_fail++; printf("FAIL: resident exceeded budget in frame %d (line %d)\n", fr, __LINE__);
            } else g_pass++;
        }
        /* the focus tile of the final frame must still be resident */
        uint32_t ftx = tx0 + (frames-1)*2, fty = ty0 + (frames-1)*2;
        wb_qkey fok = wb_qkey_make(LV, ftx, fty);
        CHECK(wb_tiles_get(big, fok, 0) != NULL, "sustained-ring focus still resident");
        wb_tiles_destroy(big);
    }

    /* evict specific tile */
    wb_tiles_evict(s, k1, 0);
    CHECK(wb_tiles_get(s, k1, 0) == NULL, "explicit evict removes tile");

    wb_tiles_destroy(s);
    printf("\n=== TILES TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

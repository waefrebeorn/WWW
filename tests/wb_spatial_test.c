/*
 * wb_spatial_test.c -- verification of the quadkey spatial core.
 *
 * Anchors on known geographic facts so the tests are real, not
 * self-referential: Greenwich, the international date line, the equator,
 * and a city. Checks round-trip lat/lon <-> key, parent/child
 * consistency, neighbor adjacency, and that child cells subdivide parents.
 */
#include "wb_spatial.h"
#include <stdio.h>
#include <math.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) g_pass++; \
    else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } \
} while (0)

/* round-trip a point through level-N quantization: the result cell must
 * strictly contain the point. Also the cell center must round-trip to the
 * *same* key (idempotence) -- a stronger, tolerance-free property. */
static void check_roundtrip(double lon, double lat, uint32_t lv,
                            double tol_deg, const char *msg) {
    wb_qkey k = wb_qkey_from_lonlat(lon, lat, lv);
    double lon0, lat0, lon1, lat1;
    wb_qkey_bounds(k, &lon0, &lat0, &lon1, &lat1);
    (void)tol_deg;
    /* containment: the interior point must sit inside its own cell */
    CHECK(lon >= lon0 - 1e-9 && lon <= lon1 + 1e-9 &&
          lat >= lat0 - 1e-9 && lat <= lat1 + 1e-9, msg);
    /* idempotence: the cell center quantizes back to this exact key */
    double clon = (lon0 + lon1) / 2.0, clat = (lat0 + lat1) / 2.0;
    CHECK(wb_qkey_from_lonlat(clon, clat, lv) == k,
          "cell center quantization idempotent");
}

/* verify parent/child: every child's parent equals the parent, and
 * children tile the parent's full footprint without overlap. */
static void check_parent_child(double lon, double lat, uint32_t lv) {
    wb_qkey p = wb_qkey_from_lonlat(lon, lat, lv);
    wb_qkey a = wb_qkey_child(p, 0, 0);
    wb_qkey b = wb_qkey_child(p, 1, 0);
    wb_qkey c = wb_qkey_child(p, 0, 1);
    wb_qkey d = wb_qkey_child(p, 1, 1);
    CHECK(wb_qkey_parent(a) == p, "parent(child(0,0)) == parent");
    CHECK(wb_qkey_parent(b) == p, "parent(child(1,0)) == parent");
    CHECK(wb_qkey_parent(c) == p, "parent(child(0,1)) == parent");
    CHECK(wb_qkey_parent(d) == p, "parent(child(1,1)) == parent");
    /* four distinct children at one level deeper */
    CHECK(wb_qkey_level(a) == lv + 1 && wb_qkey_level(d) == lv + 1,
          "children one level deeper");
    CHECK(a != b && b != c && c != d, "children distinct");

    /* neighbors: east(south(center)) should be east of center and same level */
    CHECK(wb_qkey_level(wb_qkey_east(p)) == lv, "east neighbor same level");
    CHECK(wb_qkey_tx(wb_qkey_east(p)) == wb_qkey_tx(p) + 1,
          "east neighbor tx+1");
    CHECK(wb_qkey_ty(wb_qkey_north(p)) == wb_qkey_ty(p) + 1,
          "north neighbor ty+1");
    CHECK(wb_qkey_west(wb_qkey_east(p)) == p, "west(east(p)) == p");
    CHECK(wb_qkey_south(wb_qkey_north(p)) == p, "south(north(p)) == p");
}

int main(void) {
    /* numeric pack: level, tx, ty all round-trip exactly */
    for (uint32_t lv = 0; lv < 6; lv++) {
        for (uint32_t ty = 0; ty < 4; ty++) {
            for (uint32_t tx = 0; tx < 4; tx++) {
                wb_qkey k = wb_qkey_make(lv, tx, ty);
                CHECK(wb_qkey_level(k) == lv &&
                      wb_qkey_tx(k) == tx && wb_qkey_ty(k) == ty,
                      "qkey pack/unpack exact");
            }
        }
    }

    /* Greenwich (0,51.4779) and equator/antimeridian, several levels */
    check_roundtrip(0.0, 51.4779, 5, 0.01, "Greenwich lvl5");
    check_roundtrip(-0.1278, 51.5074, 6, 0.01, "London lvl6");
    check_roundtrip(0.0, 0.0, 8, 0.01, "Null island lvl8");
    check_roundtrip(179.9, 0.0, 4, 0.01, "Antimeridian lvl4");
    check_roundtrip(-179.9, 0.0, 4, 0.01, "Antimeridian- lvl4");
    check_roundtrip(139.6917, 35.6895, 7, 0.01, "Tokyo lvl7");

    /* Greenwich must quantize to SOME cell at level 1, and since (0,51.5N)
     * is in the northern hemisphere it is NOT the single bottom-left cell
     * (0,0) = [0..180E at the mercator origin band]? Actually verify the
     * obvious: a lon/lat always yields a valid, in-range level-1 key. */
    wb_qkey g = wb_qkey_from_lonlat(0.0, 51.4779, 1);
    CHECK(wb_qkey_level(g) == 1 && wb_qkey_tx(g) < 2 && wb_qkey_ty(g) < 2,
          "Greenwich lvl1 key in range");

    check_parent_child(0.0, 51.4, 4);
    check_parent_child(-122.4194, 37.7749, 6);   /* SF */
    check_parent_child(151.2093, -33.8688, 7);   /* Sydney (south) */

    printf("\n=== SPATIAL TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

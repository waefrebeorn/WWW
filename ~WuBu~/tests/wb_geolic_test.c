/*
 * wb_geolic_test.c -- LP-110 stone 1 gate: per-cell open-data licensing.
 *
 * Devil's-advocate cases: a share-alike region cannot be washed by a finer
 * public-domain grant, so a cell inside an ODbL planet stays ODbL no matter what
 * is granted below it; a public-domain region CAN be made more restrictive from
 * below, so an ODbL enclave inside it is ODbL while its siblings stay free; the
 * governing license is the most restrictive of the cell and its ANCESTORS, never
 * the nearest record; a cell nothing covers is unlicensed and is refused rather
 * than assumed free; and a derivative carries the most restrictive of its sources,
 * which is the field the DDC must store.
 */
#include "wb_geolic.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

/* Planet "0" is ODbL, with a CDLA region and a public-domain enclave inside it.
 * Planet "2" is public domain. */
static void build(wb_geolic_ledger *l) {
    wb_geolic_init(l);
    wb_geolic_set(l, "0", "OSM", WB_GEOLIC_ODBL);
    wb_geolic_set(l, "012301", "MS-BF", WB_GEOLIC_CDLA_PERMISSIVE);
    wb_geolic_set(l, "012301230123", "USGS-MOLA", WB_GEOLIC_PUBLIC_DOMAIN);
    wb_geolic_set(l, "2", "USGS-MOLA", WB_GEOLIC_PUBLIC_DOMAIN);
}

int main(void) {
    wb_geolic_ledger l;
    build(&l);
    CHECK(wb_geolic_count(&l) == 4, "four records are registered");

    /* ---- the governing license ---- */
    wb_geolic_rec r;
    CHECK(wb_geolic_effective(&l, "0", &r) == 0 && r.license == WB_GEOLIC_ODBL &&
          strcmp(r.dataset, "OSM") == 0,
          "the planet cell is governed by its own record");
    CHECK(wb_geolic_effective(&l, "01", &r) == 0 && r.license == WB_GEOLIC_ODBL,
          "a cell under it inherits ODbL");
    CHECK(wb_geolic_effective(&l, "0123", &r) == 0 && r.license == WB_GEOLIC_ODBL,
          "three levels down, still ODbL");
    CHECK(wb_geolic_effective(&l, "012301", &r) == 0 && r.license == WB_GEOLIC_ODBL &&
          strcmp(r.dataset, "OSM") == 0,
          "a CDLA grant inside an ODbL planet does NOT wash the share-alike");
    CHECK(wb_geolic_effective(&l, "012301230123", &r) == 0 &&
          r.license == WB_GEOLIC_ODBL,
          "and a public-domain enclave does not either");
    CHECK(wb_geolic_effective(&l, "0123012301230123", &r) == 0 &&
          r.license == WB_GEOLIC_ODBL,
          "the share-alike is contagious all the way down");
    CHECK(wb_geolic_effective(&l, "2", &r) == 0 &&
          r.license == WB_GEOLIC_PUBLIC_DOMAIN &&
          strcmp(r.dataset, "USGS-MOLA") == 0,
          "another planet is public domain");
    CHECK(wb_geolic_effective(&l, "2301", &r) == 0 &&
          r.license == WB_GEOLIC_PUBLIC_DOMAIN,
          "and so is a cell under it");

    /* ---- an unlicensed cell is refused, not assumed free ---- */
    CHECK(wb_geolic_effective(&l, "", &r) == -1,
          "the root is covered by nothing");
    CHECK(wb_geolic_effective(&l, "1", &r) == -1,
          "a cell outside every record is unlicensed");
    CHECK(wb_geolic_effective(&l, "3", &r) == -1, "and so is another");
    CHECK(wb_geolic_effective(&l, "abc", &r) == -1, "a malformed cell is refused");
    CHECK(wb_geolic_effective(&l, NULL, &r) == -1, "a NULL cell is refused");
    CHECK(wb_geolic_effective(NULL, "0", &r) == -1, "a NULL ledger is refused");
    CHECK(wb_geolic_effective(&l, "0", NULL) == 0,
          "a NULL out-pointer is allowed when the cell is licensed");

    /* ---- coverage ---- */
    CHECK(wb_geolic_covers(&l, "0123") == 1, "a covered cell is covered");
    CHECK(wb_geolic_covers(&l, "0123012301230123") == 1,
          "at any depth below the record");
    CHECK(wb_geolic_covers(&l, "1") == 0, "an uncovered cell is not");
    CHECK(wb_geolic_covers(&l, "") == 0, "the root is not covered by a child");
    CHECK(wb_geolic_covers(&l, "abc") == 0, "a malformed cell is not covered");
    CHECK(wb_geolic_covers(NULL, "0") == 0, "a NULL ledger covers nothing");

    /* ---- exact lookup ---- */
    const wb_geolic_rec *f = wb_geolic_find(&l, "012301");
    CHECK(f != NULL && strcmp(f->dataset, "MS-BF") == 0,
          "a record is found by its exact cell");
    CHECK(wb_geolic_find(&l, "0123") == NULL,
          "a cell with no record of its own is not found");
    CHECK(wb_geolic_find(&l, NULL) == NULL, "a NULL cell is not found");
    CHECK(wb_geolic_find(NULL, "0") == NULL, "a NULL ledger finds nothing");

    /* ---- a public-domain region can be made restrictive from below ---- */
    wb_geolic_ledger e;
    wb_geolic_init(&e);
    wb_geolic_set(&e, "2", "USGS-MOLA", WB_GEOLIC_PUBLIC_DOMAIN);
    wb_geolic_set(&e, "23", "OSM", WB_GEOLIC_ODBL);
    CHECK(wb_geolic_effective(&e, "2", &r) == 0 &&
          r.license == WB_GEOLIC_PUBLIC_DOMAIN,
          "the public-domain record governs its own cell");
    CHECK(wb_geolic_effective(&e, "23", &r) == 0 && r.license == WB_GEOLIC_ODBL,
          "an ODbL enclave inside it is ODbL");
    CHECK(wb_geolic_effective(&e, "2301", &r) == 0 &&
          r.license == WB_GEOLIC_ODBL,
          "and so is a cell under the enclave");
    CHECK(wb_geolic_effective(&e, "20", &r) == 0 &&
          r.license == WB_GEOLIC_PUBLIC_DOMAIN,
          "a sibling of the enclave stays public domain");

    /* ---- one cell may draw from more than one dataset ---- */
    CHECK(wb_geolic_set(&l, "012301", "OSM", WB_GEOLIC_ODBL) == 0,
          "a second record on the same cell is allowed");
    CHECK(wb_geolic_count(&l) == 5, "and it is counted");
    CHECK(wb_geolic_effective(&l, "012301", &r) == 0 &&
          r.license == WB_GEOLIC_ODBL,
          "the governing license is the most restrictive of them");

    /* ---- the restrictiveness order ---- */
    CHECK(wb_geolic_combine(WB_GEOLIC_PUBLIC_DOMAIN, WB_GEOLIC_CDLA_PERMISSIVE) ==
          WB_GEOLIC_CDLA_PERMISSIVE,
          "permissive beats public domain");
    CHECK(wb_geolic_combine(WB_GEOLIC_CDLA_PERMISSIVE, WB_GEOLIC_ODBL) ==
          WB_GEOLIC_ODBL,
          "share-alike beats permissive");
    CHECK(wb_geolic_combine(WB_GEOLIC_ODBL, WB_GEOLIC_PROPRIETARY) ==
          WB_GEOLIC_PROPRIETARY,
          "proprietary beats share-alike");
    CHECK(wb_geolic_combine(WB_GEOLIC_ODBL, WB_GEOLIC_ODBL) == WB_GEOLIC_ODBL,
          "a license combined with itself is itself");
    CHECK(wb_geolic_combine(WB_GEOLIC_PROPRIETARY, WB_GEOLIC_PUBLIC_DOMAIN) ==
          WB_GEOLIC_PROPRIETARY,
          "and the order of the arguments does not matter");

    /* ---- what a derivative carries (the DDC's license field) ---- */
    const wb_geolic_license mix[] = {WB_GEOLIC_PUBLIC_DOMAIN,
                                     WB_GEOLIC_CDLA_PERMISSIVE,
                                     WB_GEOLIC_ODBL};
    CHECK(wb_geolic_fold(mix, 3) == WB_GEOLIC_ODBL,
          "a derivative of three datasets carries the most restrictive");
    const wb_geolic_license free_only[] = {WB_GEOLIC_PUBLIC_DOMAIN,
                                           WB_GEOLIC_CDLA_PERMISSIVE};
    CHECK(wb_geolic_fold(free_only, 2) == WB_GEOLIC_CDLA_PERMISSIVE,
          "two permissive datasets carry the permissive one");
    const wb_geolic_license one[] = {WB_GEOLIC_ODBL};
    CHECK(wb_geolic_fold(one, 1) == WB_GEOLIC_ODBL,
          "a single source carries its own license");
    CHECK(wb_geolic_fold(free_only, 0) == WB_GEOLIC_PUBLIC_DOMAIN,
          "no sources leaves nothing restricted");
    CHECK(wb_geolic_fold(NULL, 3) == WB_GEOLIC_PUBLIC_DOMAIN,
          "a NULL source list leaves nothing restricted");

    /* ---- the flags ---- */
    CHECK(wb_geolic_can_redistribute(WB_GEOLIC_PUBLIC_DOMAIN) == 1,
          "public domain is redistributable");
    CHECK(wb_geolic_can_redistribute(WB_GEOLIC_CDLA_PERMISSIVE) == 1,
          "permissive is redistributable");
    CHECK(wb_geolic_can_redistribute(WB_GEOLIC_ODBL) == 1,
          "share-alike is redistributable");
    CHECK(wb_geolic_can_redistribute(WB_GEOLIC_PROPRIETARY) == 0,
          "proprietary is not");
    CHECK(wb_geolic_share_alike(WB_GEOLIC_ODBL) == 1,
          "ODbL is share-alike");
    CHECK(wb_geolic_share_alike(WB_GEOLIC_CDLA_PERMISSIVE) == 0,
          "permissive is not");
    CHECK(wb_geolic_share_alike(WB_GEOLIC_PUBLIC_DOMAIN) == 0,
          "public domain is not");
    CHECK(wb_geolic_share_alike(WB_GEOLIC_PROPRIETARY) == 0,
          "proprietary is not share-alike, it is merely closed");

    /* ---- names ---- */
    CHECK(strcmp(wb_geolic_name(WB_GEOLIC_PUBLIC_DOMAIN), "public-domain") == 0,
          "a license names itself");
    CHECK(strcmp(wb_geolic_name(WB_GEOLIC_CDLA_PERMISSIVE), "CDLA-Permissive-2.0") == 0,
          "the permissive one too");
    CHECK(strcmp(wb_geolic_name(WB_GEOLIC_ODBL), "ODbL-1.0") == 0,
          "and the share-alike one");
    CHECK(strcmp(wb_geolic_name(WB_GEOLIC_PROPRIETARY), "proprietary") == 0,
          "and the closed one");
    CHECK(strcmp(wb_geolic_name((wb_geolic_license)9), "?") == 0,
          "an unknown license has no name");

    /* ---- set refuses bad input ---- */
    CHECK(wb_geolic_set(&l, "abc", "OSM", WB_GEOLIC_ODBL) == -1,
          "a malformed cell is refused");
    CHECK(wb_geolic_set(&l, NULL, "OSM", WB_GEOLIC_ODBL) == -1,
          "a NULL cell is refused");
    CHECK(wb_geolic_set(&l, "0123", NULL, WB_GEOLIC_ODBL) == -1,
          "a NULL dataset is refused");
    CHECK(wb_geolic_set(&l, "0123", "", WB_GEOLIC_ODBL) == -1,
          "an empty dataset is refused");
    CHECK(wb_geolic_set(&l, "0123",
                        "0123456789012345678901234567890123456789012345678901234567890123456789",
                        WB_GEOLIC_ODBL) == -1,
          "an over-long dataset name is refused");
    CHECK(wb_geolic_set(&l, "0123", "OSM", (wb_geolic_license)9) == -1,
          "an unknown license is refused");
    CHECK(wb_geolic_set(NULL, "0123", "OSM", WB_GEOLIC_ODBL) == -1,
          "a NULL ledger refuses");
    CHECK(wb_geolic_count(&l) == 5, "the refused sets changed nothing");

    /* ---- a record on the root covers everything ---- */
    wb_geolic_ledger root;
    wb_geolic_init(&root);
    CHECK(wb_geolic_set(&root, "", "OSM", WB_GEOLIC_ODBL) == 0,
          "a record may sit on the root");
    CHECK(wb_geolic_effective(&root, "", &r) == 0 &&
          r.license == WB_GEOLIC_ODBL,
          "and then even the root is licensed");
    CHECK(wb_geolic_effective(&root, "1", &r) == 0 &&
          r.license == WB_GEOLIC_ODBL,
          "and a cell that was unlicensed now inherits it");
    CHECK(wb_geolic_covers(&root, "3") == 1, "every cell is covered");

    /* ---- the ledger has a ceiling ---- */
    wb_geolic_ledger full;
    wb_geolic_init(&full);
    for (uint32_t i = 0; i < WB_GEOLIC_MAX; i++) {
        char cell[8];
        uint32_t v = i;
        for (int k = 4; k >= 0; k--) {
            cell[k] = (char)('0' + (v & 3));
            v >>= 2;
        }
        cell[5] = '\0';
        CHECK(wb_geolic_set(&full, cell, "OSM", WB_GEOLIC_ODBL) == 0,
              "a record is added up to the ceiling");
    }
    CHECK(wb_geolic_count(&full) == WB_GEOLIC_MAX, "the ledger fills to its ceiling");
    CHECK(wb_geolic_set(&full, "33", "MS-BF", WB_GEOLIC_CDLA_PERMISSIVE) == -1,
          "and then refuses another");
    CHECK(wb_geolic_count(&full) == WB_GEOLIC_MAX, "the ledger is full");

    /* ---- degenerate input ---- */
    wb_geolic_init(NULL);
    CHECK(wb_geolic_count(NULL) == 0, "a NULL ledger has no records");

    /* ---- determinism: the same records agree ---- */
    wb_geolic_ledger d1, d2;
    build(&d1);
    build(&d2);
    const char *cells[] = {"0", "01", "0123", "012301", "012301230123",
                           "0123012301230123", "2", "2301", "", "1", "abc"};
    int same = 1;
    for (size_t i = 0; i < sizeof cells / sizeof cells[0]; i++) {
        wb_geolic_rec a, b;
        int ra = wb_geolic_effective(&d1, cells[i], &a);
        int rb = wb_geolic_effective(&d2, cells[i], &b);
        if (ra != rb) same = 0;
        else if (ra == 0 && (a.license != b.license ||
                             strcmp(a.dataset, b.dataset) != 0))
            same = 0;
    }
    CHECK(same, "two ledgers built the same way agree on every cell");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

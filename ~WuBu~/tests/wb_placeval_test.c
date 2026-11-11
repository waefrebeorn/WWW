/*
 * wb_placeval_test.c -- LP-112 stone 1 gate: AGI-authored place validation.
 *
 * Devil's-advocate cases: a place is a REGION, so a bag of cells scattered across
 * the planet is refused rather than published as one place; a place must sit
 * inside the parent volume it declares, so a place that reaches outside it is
 * refused; an elevation sample must agree with the source DEM, and a cell the
 * source does not cover cannot be given an invented elevation; a cell that is
 * already reserved cannot be published into; every cell must be licensed, and
 * the license the place declares must be at least as restrictive as the most
 * restrictive license it draws on -- a place cannot be published more freely than
 * the data inside it. A check the caller cannot make is skipped, not assumed.
 */
#include "wb_placeval.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

#define L 2   /* the level every place in this gate sits at */

static wb_place mkplace(const char *name, const char *parent, wb_geolic_license lic) {
    wb_place p;
    memset(&p, 0, sizeof p);
    snprintf(p.name, sizeof p.name, "%s", name);
    snprintf(p.parent, sizeof p.parent, "%s", parent);
    p.license = lic;
    return p;
}

static void addxy(wb_place *p, uint32_t col, uint32_t row) {
    wb_qkey_encode(L, col, row, p->cells[p->n], WB_QKEY_MAX);
    p->n++;
}

static void addcell(wb_place *p, const char *cell) {
    snprintf(p->cells[p->n], WB_QKEY_MAX, "%s", cell);
    p->n++;
}

static void addsample(wb_place *p, const char *cell, int32_t elevation) {
    snprintf(p->samples[p->nsamples].cell, WB_QKEY_MAX, "%s", cell);
    p->samples[p->nsamples].elevation = elevation;
    p->nsamples++;
}

/* A 2x2 block at the origin: four cells, all under parent "0". */
static void block(wb_place *p) {
    addxy(p, 0, 0);
    addxy(p, 1, 0);
    addxy(p, 0, 1);
    addxy(p, 1, 1);
}

static void build_lic(wb_geolic_ledger *lic) {
    wb_geolic_init(lic);
    wb_geolic_set(lic, "0", "OSM", WB_GEOLIC_ODBL);
}
int main(void) {
    wb_placeval_ctx ctx;
    memset(&ctx, 0, sizeof ctx);
    wb_geolic_ledger lic;

    /* ---- structure ---- */
    wb_place p = mkplace("empty", "0", WB_GEOLIC_ODBL);
    uint32_t reason = WB_PLACEVAL_OK;
    CHECK(wb_placeval_check(&p, &ctx, &reason) == -1 &&
          reason == WB_PLACEVAL_ERR_EMPTY,
          "a place with no cells is refused");

    p = mkplace("bad", "0", WB_GEOLIC_ODBL);
    addcell(&p, "abc");
    CHECK(wb_placeval_check(&p, &ctx, &reason) == -1 &&
          reason == WB_PLACEVAL_ERR_BAD_CELL,
          "a malformed cell is refused");

    p = mkplace("mixed", "0", WB_GEOLIC_ODBL);
    addcell(&p, "00");
    addcell(&p, "010");
    CHECK(wb_placeval_check(&p, &ctx, &reason) == -1 &&
          reason == WB_PLACEVAL_ERR_MIXED_DEPTH,
          "cells at more than one level are refused");

    p = mkplace("scattered", "0", WB_GEOLIC_ODBL);
    addxy(&p, 0, 0);
    addxy(&p, 3, 3);
    CHECK(wb_placeval_check(&p, &ctx, &reason) == -1 &&
          reason == WB_PLACEVAL_ERR_DISCONNECTED,
          "a bag of cells is not one region");

    p = mkplace("diagonal", "0", WB_GEOLIC_ODBL);
    addxy(&p, 0, 0);
    addxy(&p, 1, 1);
    CHECK(wb_placeval_check(&p, &ctx, &reason) == -1 &&
          reason == WB_PLACEVAL_ERR_DISCONNECTED,
          "two cells touching only at a corner are not adjacent");

    p = mkplace("ok", "0", WB_GEOLIC_ODBL);
    block(&p);
    CHECK(wb_placeval_check(&p, &ctx, &reason) == 0 && reason == WB_PLACEVAL_OK,
          "a 2x2 block is one region");

    p = mkplace("single", "0", WB_GEOLIC_ODBL);
    addxy(&p, 1, 1);
    CHECK(wb_placeval_check(&p, &ctx, &reason) == 0,
          "a single cell is a region of one");

    /* ---- containment ---- */
    p = mkplace("outside", "1", WB_GEOLIC_ODBL);
    block(&p);
    CHECK(wb_placeval_check(&p, &ctx, &reason) == -1 &&
          reason == WB_PLACEVAL_ERR_NOT_CONTAINED,
          "a place outside its declared parent is refused");

    p = mkplace("badparent", "abc", WB_GEOLIC_ODBL);
    block(&p);
    CHECK(wb_placeval_check(&p, &ctx, &reason) == -1 &&
          reason == WB_PLACEVAL_ERR_NOT_CONTAINED,
          "an invalid parent contains nothing");

    p = mkplace("rootparent", "", WB_GEOLIC_ODBL);
    block(&p);
    CHECK(wb_placeval_check(&p, &ctx, &reason) == 0,
          "the root is a valid parent and contains everything");

    /* ---- source DEM ---- */
    p = mkplace("dem-ok", "0", WB_GEOLIC_ODBL);
    block(&p);
    addsample(&p, "00", 1200);
    addsample(&p, "01", -300);
    wb_placeval_sample dem[] = {{"00", 1200}, {"01", -300}};
    ctx.dem = dem;
    ctx.ndem = 2;
    CHECK(wb_placeval_check(&p, &ctx, &reason) == 0,
          "samples that agree with the source DEM are accepted");

    addsample(&p, "02", 5);
    CHECK(wb_placeval_check(&p, &ctx, &reason) == -1 &&
          reason == WB_PLACEVAL_ERR_DEM_MISMATCH,
          "a sample for a cell the source does not cover is refused");

    p.samples[0].elevation = 1201;
    CHECK(wb_placeval_check(&p, &ctx, &reason) == -1 &&
          reason == WB_PLACEVAL_ERR_DEM_MISMATCH,
          "a sample that disagrees with the source is refused");

    ctx.dem = NULL;
    ctx.ndem = 0;
    CHECK(wb_placeval_check(&p, &ctx, &reason) == 0,
          "with no source supplied the DEM check is skipped, not assumed");

    /* ---- reservation ---- */
    p = mkplace("reserved", "0", WB_GEOLIC_ODBL);
    block(&p);
    snprintf(ctx.reserved[0], WB_QKEY_MAX, "01");
    ctx.nreserved = 1;
    CHECK(wb_placeval_check(&p, &ctx, &reason) == -1 &&
          reason == WB_PLACEVAL_ERR_RESERVED,
          "a cell that is already reserved cannot be published into");

    snprintf(ctx.reserved[0], WB_QKEY_MAX, "22");
    CHECK(wb_placeval_check(&p, &ctx, &reason) == 0,
          "a reservation outside the place does not block it");
    ctx.nreserved = 0;

    /* ---- rights ---- */
    build_lic(&lic);
    ctx.lic = &lic;
    p = mkplace("odbl-ok", "", WB_GEOLIC_ODBL);
    block(&p);
    CHECK(wb_placeval_check(&p, &ctx, &reason) == 0,
          "a place declaring ODbL over ODbL cells is accepted");

    p = mkplace("too-free", "", WB_GEOLIC_PUBLIC_DOMAIN);
    block(&p);
    CHECK(wb_placeval_check(&p, &ctx, &reason) == -1 &&
          reason == WB_PLACEVAL_ERR_LICENSE_WEAK,
          "a place cannot be published more freely than its sources");

    p = mkplace("unlicensed", "", WB_GEOLIC_ODBL);
    block(&p);
    addxy(&p, 2, 0);           /* adjacent to the block, inside the root, outside "0" */
    CHECK(wb_placeval_check(&p, &ctx, &reason) == -1 &&
          reason == WB_PLACEVAL_ERR_UNLICENSED,
          "a cell with no governing license blocks the place");

    ctx.lic = NULL;
    CHECK(wb_placeval_check(&p, &ctx, &reason) == 0,
          "with no license ledger supplied the rights check is skipped");

    /* ---- the order of the checks ---- */
    p = mkplace("many-faults", "1", WB_GEOLIC_PUBLIC_DOMAIN);
    addxy(&p, 0, 0);
    addxy(&p, 3, 3);           /* disconnected, outside the parent, unlicensed */
    ctx.lic = &lic;
    CHECK(wb_placeval_check(&p, &ctx, &reason) == -1 &&
          reason == WB_PLACEVAL_ERR_DISCONNECTED,
          "structure is reported before containment and rights");

    p = mkplace("containment-first", "1", WB_GEOLIC_PUBLIC_DOMAIN);
    block(&p);
    CHECK(wb_placeval_check(&p, &ctx, &reason) == -1 &&
          reason == WB_PLACEVAL_ERR_NOT_CONTAINED,
          "containment is reported before rights");

    /* ---- a fully valid place ---- */
    p = mkplace("valid", "0", WB_GEOLIC_ODBL);
    block(&p);
    addsample(&p, "00", 1200);
    wb_placeval_sample dem2[] = {{"00", 1200}};
    ctx.dem = dem2;
    ctx.ndem = 1;
    snprintf(ctx.reserved[0], WB_QKEY_MAX, "33");
    ctx.nreserved = 1;
    CHECK(wb_placeval_check(&p, &ctx, &reason) == 0 && reason == WB_PLACEVAL_OK,
          "a place that passes every check is accepted");

    /* ---- reason names ---- */
    CHECK(strcmp(wb_placeval_reason_name(WB_PLACEVAL_OK), "ok") == 0,
          "a reason names itself");
    CHECK(strcmp(wb_placeval_reason_name(WB_PLACEVAL_ERR_DISCONNECTED),
                 "the cells do not form one region") == 0,
          "the connectivity reason names itself");
    CHECK(strcmp(wb_placeval_reason_name(WB_PLACEVAL_ERR_LICENSE_WEAK),
                 "the declared license is weaker than the sources") == 0,
          "the rights reason names itself");
    CHECK(strcmp(wb_placeval_reason_name(99), "?") == 0,
          "an unknown reason has no name");

    /* ---- degenerate input ---- */
    CHECK(wb_placeval_check(NULL, &ctx, &reason) == -1 &&
          reason == WB_PLACEVAL_ERR_EMPTY,
          "a NULL place is refused as empty");
    p = mkplace("no-ctx", "0", WB_GEOLIC_ODBL);
    block(&p);
    CHECK(wb_placeval_check(&p, NULL, &reason) == 0,
          "a NULL context applies no constraint");
    CHECK(wb_placeval_check(&p, &ctx, NULL) == 0,
          "a NULL reason out-pointer is allowed");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

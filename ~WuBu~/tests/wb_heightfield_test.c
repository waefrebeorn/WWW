/*
 * wb_heightfield_test.c -- LP-43 stone 1 gate: a terrain heightfield.
 *
 * Devil's-advocate cases: the mip chain must be exact, so an odd resolution is
 * required and an even one is refused rather than silently mis-sampled; level 0
 * is the field itself and has zero error; the geometric error GROWS with the
 * level, so a coarse level is never chosen for a tolerance a finer one would fail;
 * a flat field collapses to the deepest level at any tolerance, while a curved one
 * does not; and the level is chosen by what the simplification COSTS, so the same
 * tolerance picks different levels for different fields.
 */
#include "wb_heightfield.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

#define RES 5

/* A parabola across u: 4 1 0 1 4, the same in every row. */
static const int16_t PARAB[RES * RES] = {
    4, 1, 0, 1, 4,
    4, 1, 0, 1, 4,
    4, 1, 0, 1, 4,
    4, 1, 0, 1, 4,
    4, 1, 0, 1, 4
};

/* A ramp across u: 0 1 2 3 4. */
static const int16_t RAMP[RES * RES] = {
    0, 1, 2, 3, 4,
    0, 1, 2, 3, 4,
    0, 1, 2, 3, 4,
    0, 1, 2, 3, 4,
    0, 1, 2, 3, 4
};

/* Flat. */
static const int16_t FLAT[RES * RES] = {
    7, 7, 7, 7, 7,
    7, 7, 7, 7, 7,
    7, 7, 7, 7, 7,
    7, 7, 7, 7, 7,
    7, 7, 7, 7, 7
};

int main(void) {
    wb_heightfield hf;

    /* ---- the shape of the field ---- */
    wb_heightfield_init(&hf, RES, PARAB);
    CHECK(wb_heightfield_valid(&hf) == 1, "a 5x5 field is usable");
    CHECK(wb_heightfield_res(&hf) == RES, "and it reports its resolution");
    CHECK(wb_heightfield_max_level(&hf) == 2, "5 = 2^2 + 1, so two levels down");
    CHECK(wb_heightfield_level_res(&hf, 0) == 5, "level 0 is the full field");
    CHECK(wb_heightfield_level_res(&hf, 1) == 3, "level 1 halves the span");
    CHECK(wb_heightfield_level_res(&hf, 2) == 2, "level 2 keeps both endpoints");
    CHECK(wb_heightfield_level_res(&hf, 3) == 2, "a level past the deepest is clamped");
    CHECK(wb_heightfield_level_res(&hf, 99) == 2, "and so is a far one");
    CHECK(wb_heightfield_min(&hf) == 0, "the lowest point is 0");
    CHECK(wb_heightfield_max(&hf) == 4, "the highest is 4");

    /* ---- an odd resolution is required ---- */
    wb_heightfield bad;
    wb_heightfield_init(&bad, 4, PARAB);
    CHECK(wb_heightfield_valid(&bad) == 0, "an even resolution is refused");
    CHECK(wb_heightfield_max_level(&bad) == 0, "so it has no levels");
    CHECK(wb_heightfield_level_res(&bad, 0) == 0, "and no level resolutions");
    CHECK(wb_heightfield_sample(&bad, 0.5f, 0.5f) == 0.0f,
          "and sampling it returns nothing");
    CHECK(wb_heightfield_level_error(&bad, 1) == 0.0f,
          "and its error is reported as zero");
    CHECK(wb_heightfield_pick_level(&bad, 100.0f) == 0,
          "and it picks the full field");

    wb_heightfield_init(&bad, 1, PARAB);
    CHECK(wb_heightfield_valid(&bad) == 0, "a 1x1 field is refused");
    wb_heightfield_init(&bad, 0, PARAB);
    CHECK(wb_heightfield_valid(&bad) == 0, "a zero resolution is refused");
    wb_heightfield_init(&bad, RES, NULL);
    CHECK(wb_heightfield_valid(&bad) == 0, "a NULL grid is refused");
    CHECK(wb_heightfield_valid(NULL) == 0, "a NULL field is refused");
    CHECK(wb_heightfield_res(NULL) == 0, "a NULL field has no resolution");
    wb_heightfield_init(NULL, RES, PARAB);

    /* ---- sampling ---- */
    CHECK(wb_heightfield_sample(&hf, 0.0f, 0.0f) == 4.0f, "the corner is 4");
    CHECK(wb_heightfield_sample(&hf, 1.0f, 1.0f) == 4.0f, "the other corner too");
    CHECK(wb_heightfield_sample(&hf, 0.5f, 0.5f) == 0.0f, "the middle is 0");
    CHECK(wb_heightfield_sample(&hf, 0.25f, 0.0f) == 1.0f,
          "a quarter of the way is 1");
    CHECK(wb_heightfield_sample(&hf, 0.75f, 1.0f) == 1.0f,
          "and three quarters is 1");
    CHECK(wb_heightfield_sample(&hf, -1.0f, -1.0f) == 4.0f,
          "outside the field clamps to the edge");
    CHECK(wb_heightfield_sample(&hf, 2.0f, 2.0f) == 4.0f, "in both directions");

    /* ---- normals ---- */
    float nx = 0, ny = 0, nz = 0;
    wb_heightfield_normal(&hf, 0.5f, 0.5f, &nx, &ny, &nz);
    CHECK(fabsf(nx) < 1e-6f && fabsf(ny) < 1e-6f && fabsf(nz - 1.0f) < 1e-6f,
          "the top of the bowl points straight up");
    wb_heightfield_normal(&hf, 0.0f, 0.5f, &nx, &ny, &nz);
    float len = sqrtf(nx * nx + ny * ny + nz * nz);
    CHECK(fabsf(len - 1.0f) < 1e-5f, "the normal is a unit vector");
    CHECK(nz > 0.0f, "and it points up");
    CHECK(nx > 0.0f,
          "on the left wall of the bowl it leans toward +u, where the ground rises");
    wb_heightfield_normal(&hf, 1.0f, 0.5f, &nx, &ny, &nz);
    CHECK(nx < 0.0f, "and on the right wall toward -u");
    wb_heightfield_normal(&hf, 0.5f, 0.5f, NULL, NULL, NULL);
    wb_heightfield_normal(NULL, 0.5f, 0.5f, &nx, &ny, &nz);
    CHECK(nx == 0.0f && ny == 0.0f && nz == 1.0f,
          "a NULL field yields the default up vector");

    /* ---- the geometric error grows with the level ---- */
    CHECK(wb_heightfield_level_error(&hf, 0) == 0.0f,
          "level 0 is the field, so it is exact");
    CHECK(wb_heightfield_level_error(&hf, 1) == 1.0f,
          "level 1 of the parabola is off by one unit");
    CHECK(wb_heightfield_level_error(&hf, 2) == 4.0f,
          "level 2 is off by four");
    CHECK(wb_heightfield_level_error(&hf, 99) == 4.0f,
          "a level past the deepest is clamped to it");

    /* ---- the level is chosen by what it costs ---- */
    CHECK(wb_heightfield_pick_level(&hf, 0.0f) == 0,
          "a tolerance of zero buys the full field of a curved one");
    CHECK(wb_heightfield_pick_level(&hf, 0.5f) == 0,
          "half a unit of error is not enough for level 1");
    CHECK(wb_heightfield_pick_level(&hf, 1.0f) == 1,
          "one unit buys level 1");
    CHECK(wb_heightfield_pick_level(&hf, 3.9f) == 1,
          "just under four is not enough for level 2");
    CHECK(wb_heightfield_pick_level(&hf, 4.0f) == 2,
          "four units buys the deepest level");
    CHECK(wb_heightfield_pick_level(&hf, 1000.0f) == 2,
          "and a huge tolerance is still clamped to the deepest level");

    /* ---- a flat field costs nothing to simplify ---- */
    wb_heightfield flat;
    wb_heightfield_init(&flat, RES, FLAT);
    CHECK(wb_heightfield_level_error(&flat, 1) == 0.0f,
          "a flat field is exact at level 1");
    CHECK(wb_heightfield_level_error(&flat, 2) == 0.0f,
          "and at level 2");
    CHECK(wb_heightfield_pick_level(&flat, 0.0f) == 2,
          "so it collapses to the deepest level at any tolerance");
    CHECK(wb_heightfield_min(&flat) == 7 && wb_heightfield_max(&flat) == 7,
          "and its range is a single value");

    /* ---- a ramp is linear, so every level is exact ---- */
    wb_heightfield ramp;
    wb_heightfield_init(&ramp, RES, RAMP);
    CHECK(wb_heightfield_level_error(&ramp, 1) == 0.0f,
          "a ramp survives level 1 exactly");
    CHECK(wb_heightfield_level_error(&ramp, 2) == 0.0f,
          "and level 2");
    CHECK(wb_heightfield_pick_level(&ramp, 0.0f) == 2,
          "so a ramp also collapses to the deepest level");

    /* ---- a coarser field has fewer levels ---- */
    int16_t tiny[9] = {0, 1, 2, 0, 1, 2, 0, 1, 2};
    wb_heightfield small;
    wb_heightfield_init(&small, 3, tiny);
    CHECK(wb_heightfield_valid(&small) == 1, "a 3x3 field is usable");
    CHECK(wb_heightfield_max_level(&small) == 1, "3 = 2^1 + 1, so one level down");
    CHECK(wb_heightfield_level_res(&small, 1) == 2, "level 1 keeps both endpoints");
    CHECK(wb_heightfield_sample(&small, 0.5f, 0.5f) == 1.0f,
          "and it samples the middle");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

/*
 * wb_validate_test.c -- LP-44 stone 1 gate: mesh import validation.
 *
 * Devil's-advocate cases: each problem is counted separately and reported without
 * stopping at the first, an out-of-range triangle is not also measured for area,
 * and a mesh with no normals skips the normal check entirely.
 */
#include "wb_validate.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

/* A unit square in the XY plane, two triangles, with unit normals. */
static const float square_pos[4 * 3] = {
    0, 0, 0,
    1, 0, 0,
    1, 1, 0,
    0, 1, 0
};
static const uint32_t square_idx[6] = { 0, 1, 2, 0, 2, 3 };
static const float square_nrm[4 * 3] = {
    0, 0, 1,
    0, 0, 1,
    0, 0, 1,
    0, 0, 1
};

int main(void) {
    wb_validate_report rep;

    /* ---- a clean mesh ---- */
    wb_mesh_data good = { square_pos, 4, square_idx, 6, square_nrm };
    CHECK(wb_validate_mesh(&good, &rep) == 0, "a clean mesh validates");
    CHECK(rep.triangles == 2 && rep.out_of_range == 0 && rep.degenerate == 0 &&
          rep.bad_normal == 0 && rep.bad_vertex == 0, "the clean report is all zeroes");

    /* ---- an out-of-range index ---- */
    uint32_t bad_idx[6] = { 0, 1, 2, 0, 2, 9 };
    wb_mesh_data oor = { square_pos, 4, bad_idx, 6, square_nrm };
    CHECK(wb_validate_mesh(&oor, &rep) == 1, "an out-of-range index fails validation");
    CHECK(rep.out_of_range == 1, "one out-of-range triangle is counted");
    CHECK(rep.degenerate == 0, "the out-of-range triangle is not measured for area");
    CHECK(rep.triangles == 2, "the triangle count is still reported");

    /* ---- a degenerate triangle ---- */
    uint32_t deg_idx[6] = { 0, 1, 1, 0, 2, 3 };
    wb_mesh_data deg = { square_pos, 4, deg_idx, 6, square_nrm };
    CHECK(wb_validate_mesh(&deg, &rep) == 1, "a degenerate triangle fails validation");
    CHECK(rep.degenerate == 1, "one degenerate triangle is counted");
    CHECK(rep.out_of_range == 0, "the degenerate triangle is in range");

    /* ---- a non-unit normal ---- */
    float bad_nrm[4 * 3] = {
        0, 0, 1,
        0, 0, 2,      /* not unit length */
        0, 0, 1,
        0, 0, 1
    };
    wb_mesh_data nrm = { square_pos, 4, square_idx, 6, bad_nrm };
    CHECK(wb_validate_mesh(&nrm, &rep) == 1, "a non-unit normal fails validation");
    CHECK(rep.bad_normal == 1, "one bad normal is counted");
    CHECK(rep.degenerate == 0, "the geometry is still clean");

    /* ---- a NaN vertex ---- */
    float nan_pos[4 * 3] = {
        0, 0, 0,
        1, 0, 0,
        NAN, 1, 0,
        0, 1, 0
    };
    wb_mesh_data nan = { nan_pos, 4, square_idx, 6, square_nrm };
    CHECK(wb_validate_mesh(&nan, &rep) == 1, "a NaN vertex fails validation");
    CHECK(rep.bad_vertex == 1, "one bad vertex is counted");

    /* ---- an infinite vertex ---- */
    float inf_pos[4 * 3] = {
        0, 0, 0,
        1, 0, 0,
        INFINITY, 1, 0,
        0, 1, 0
    };
    wb_mesh_data inf = { inf_pos, 4, square_idx, 6, square_nrm };
    CHECK(wb_validate_mesh(&inf, &rep) == 1, "an infinite vertex fails validation");
    CHECK(rep.bad_vertex == 1, "one infinite vertex is counted");

    /* ---- several problems at once are all counted ---- */
    uint32_t multi_idx[6] = { 0, 1, 1, 0, 2, 7 };
    wb_mesh_data multi = { nan_pos, 4, multi_idx, 6, bad_nrm };
    CHECK(wb_validate_mesh(&multi, &rep) == 1, "a mesh with several problems fails");
    CHECK(rep.bad_vertex == 1 && rep.degenerate == 1 && rep.out_of_range == 1 && rep.bad_normal == 1,
          "every problem is counted, not just the first");

    /* ---- no normals: the normal check is skipped ---- */
    wb_mesh_data nonrm = { square_pos, 4, square_idx, 6, NULL };
    CHECK(wb_validate_mesh(&nonrm, &rep) == 0, "a mesh with no normals validates");
    CHECK(rep.bad_normal == 0, "the normal check is skipped when there are no normals");

    /* ---- no indices: the triangle checks are skipped ---- */
    wb_mesh_data noidx = { square_pos, 4, NULL, 0, square_nrm };
    CHECK(wb_validate_mesh(&noidx, &rep) == 0, "a mesh with no indices validates");
    CHECK(rep.triangles == 0, "the triangle count is zero");

    /* ---- problem names ---- */
    CHECK(strcmp(wb_validate_problem_name(1u << 0), "bad_vertex") == 0, "the vertex problem is named");
    CHECK(strcmp(wb_validate_problem_name(1u << 1), "out_of_range") == 0, "the index problem is named");
    CHECK(strcmp(wb_validate_problem_name(1u << 2), "degenerate") == 0, "the degenerate problem is named");
    CHECK(strcmp(wb_validate_problem_name(1u << 3), "bad_normal") == 0, "the normal problem is named");
    CHECK(strcmp(wb_validate_problem_name(1u << 9), "unknown") == 0, "an unknown bit is named");

    /* ---- determinism ---- */
    wb_validate_report a, b;
    wb_validate_mesh(&multi, &a);
    wb_validate_mesh(&multi, &b);
    CHECK(memcmp(&a, &b, sizeof a) == 0, "validation is deterministic");

    /* ---- degenerate input ---- */
    CHECK(wb_validate_mesh(NULL, &rep) == 1, "a NULL mesh fails validation");
    CHECK(wb_validate_mesh(&good, NULL) == 1, "a NULL report is refused");
    wb_mesh_data nopos = { NULL, 0, square_idx, 6, square_nrm };
    CHECK(wb_validate_mesh(&nopos, &rep) == 1, "a mesh with no positions fails validation");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

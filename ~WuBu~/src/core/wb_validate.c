/*
 * wb_validate.c -- mesh import validation (LP-44 stone 1). See wb_validate.h.
 */
#include "wb_validate.h"

#include <math.h>
#include <string.h>

/* The problem bits, in report order. */
enum {
    WB_VALIDATE_BAD_VERTEX  = 1u << 0,
    WB_VALIDATE_OUT_OF_RANGE= 1u << 1,
    WB_VALIDATE_DEGENERATE  = 1u << 2,
    WB_VALIDATE_BAD_NORMAL  = 1u << 3
};

const char *wb_validate_problem_name(uint32_t bit) {
    switch (bit) {
    case WB_VALIDATE_BAD_VERTEX:   return "bad_vertex";
    case WB_VALIDATE_OUT_OF_RANGE: return "out_of_range";
    case WB_VALIDATE_DEGENERATE:   return "degenerate";
    case WB_VALIDATE_BAD_NORMAL:   return "bad_normal";
    default:                       return "unknown";
    }
}

static int wb_validate_finite(float f) { return isfinite(f); }

int wb_validate_mesh(const wb_mesh_data *m, wb_validate_report *rep) {
    if (!rep) return 1;
    memset(rep, 0, sizeof *rep);
    if (!m) return 1;
    if (!m->positions || m->npos == 0) return 1;
    rep->triangles = m->indices ? m->nidx / 3 : 0;

    /* ---- vertex positions: finite ---- */
    for (uint32_t i = 0; i < m->npos; i++) {
        const float *p = m->positions + (size_t)i * 3;
        if (!wb_validate_finite(p[0]) || !wb_validate_finite(p[1]) || !wb_validate_finite(p[2])) {
            rep->bad_vertex++;
        }
    }

    /* ---- indices: in range, and no degenerate triangle ---- */
    if (m->indices) {
        for (uint32_t t = 0; t < rep->triangles; t++) {
            uint32_t a = m->indices[(size_t)t * 3 + 0];
            uint32_t b = m->indices[(size_t)t * 3 + 1];
            uint32_t c = m->indices[(size_t)t * 3 + 2];
            if (a >= m->npos || b >= m->npos || c >= m->npos) {
                rep->out_of_range++;
                continue;   /* the triangle cannot be measured */
            }
            const float *pa = m->positions + (size_t)a * 3;
            const float *pb = m->positions + (size_t)b * 3;
            const float *pc = m->positions + (size_t)c * 3;
            float ux = pb[0] - pa[0], uy = pb[1] - pa[1], uz = pb[2] - pa[2];
            float vx = pc[0] - pa[0], vy = pc[1] - pa[1], vz = pc[2] - pa[2];
            float cx = uy * vz - uz * vy;
            float cy = uz * vx - ux * vz;
            float cz = ux * vy - uy * vx;
            if (cx * cx + cy * cy + cz * cz == 0.0f) rep->degenerate++;
        }
    }

    /* ---- normals: unit length ---- */
    if (m->normals) {
        for (uint32_t i = 0; i < m->npos; i++) {
            const float *n = m->normals + (size_t)i * 3;
            float len2 = n[0] * n[0] + n[1] * n[1] + n[2] * n[2];
            if (!wb_validate_finite(n[0]) || !wb_validate_finite(n[1]) || !wb_validate_finite(n[2]) ||
                len2 < 0.9999f || len2 > 1.0001f) {
                rep->bad_normal++;
            }
        }
    }

    return (rep->bad_vertex || rep->out_of_range || rep->degenerate || rep->bad_normal) ? 1 : 0;
}

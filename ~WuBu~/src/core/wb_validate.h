/*
 * wb_validate.h -- mesh import validation (LP-44 stone 1).
 *
 * A mesh that reaches the renderer with an out-of-range index, a zero-area
 * triangle, a non-unit normal, or a NaN vertex is a crash or a black artifact
 * waiting to happen. Validation runs at import time, on the DCC side of the
 * pipeline, and reports every problem it finds rather than stopping at the first.
 *
 * C11, no deps.
 */
#ifndef WB_VALIDATE_H
#define WB_VALIDATE_H

#include <stdint.h>

typedef struct {
    const float   *positions;   /* 3 floats per vertex */
    uint32_t       npos;
    const uint32_t *indices;    /* 3 per triangle */
    uint32_t       nidx;
    const float   *normals;     /* 3 per vertex, may be NULL */
} wb_mesh_data;

typedef struct {
    uint32_t  triangles;        /* nidx / 3 */
    uint32_t  out_of_range;     /* indices >= npos */
    uint32_t  degenerate;       /* zero-area triangles */
    uint32_t  bad_normal;       /* normals that are not unit length */
    uint32_t  bad_vertex;       /* NaN or infinite positions */
} wb_validate_report;

/* Validate a mesh. Returns 0 when the mesh is clean, 1 when any problem was
 * found (the report is filled either way). */
int wb_validate_mesh(const wb_mesh_data *m, wb_validate_report *rep);

/* A short human-readable name for a problem field, for logs. */
const char *wb_validate_problem_name(uint32_t bit);

#endif /* WB_VALIDATE_H */

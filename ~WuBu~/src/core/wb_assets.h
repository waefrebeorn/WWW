/*
 * wb_assets.h -- WuBu Engine content pipeline: glTF/GLB model loading.
 *
 * The open path: glTF 2.0 / GLB in, engine-ready mesh out. The derived-data
 * cache and the manifest live in their own headers (wb_ddc.h, wb_manifest.h)
 * so a caller that only loads models does not pull them in.
 *
 * C11, no external deps.
 */
#ifndef WB_ASSETS_H
#define WB_ASSETS_H

#include <stddef.h>
#include <stdint.h>
#include "wb_util.h"   /* wb_content_hash */

#ifdef __cplusplus
extern "C" {
#endif

/* Engine vertex: position + normal. Matches the GPU backend's stride. */
typedef struct {
    float pos[3];
    float normal[3];
} wb_vertex;

typedef struct {
    char     name[128];
    wb_vertex *verts;
    int       nverts;
    uint32_t *indices;
    int       nindices;
    float     base_color[4];
    float     metallic;
    float     roughness;
} wb_mesh;

typedef struct {
    char   source[512];      /* path the model came from */
    wb_mesh *meshes;
    int     nmeshes;
} wb_model;

void wb_model_free(wb_model *m);

/* Load a .gltf (JSON + external/embedded buffer) or .glb (binary container).
 * Returns 0 on success; on failure a message is available from
 * wb_asset_error(). */
int  wb_asset_load_gltf(const char *path, wb_model *out);
const char *wb_asset_error(void);

#ifdef __cplusplus
}
#endif

#endif /* WB_ASSETS_H */

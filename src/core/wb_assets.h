/*
 * wb_assets.h -- WuBu Engine content pipeline (slice 12).
 *
 * The open path: glTF 2.0 / GLB in, engine-ready mesh out, with a
 * content-addressed derived-data cache and a manifest -- the pipeline that
 * makes generated or imported content cheap to ship (the assessment's pillar
 * 3, and the thing that lets creators drop a model into the world).
 *
 * Design follows the open-format pipeline used by the open-mmorpg spec and
 * Redot's glTF module (thirdparty/redot, reference): glTF is the source of
 * truth, no proprietary converter, derived data is keyed by the source's
 * content hash so an incremental build never trusts a timestamp.
 *
 * C11, no external deps.
 */
#ifndef WB_ASSETS_H
#define WB_ASSETS_H

#include <stddef.h>
#include <stdint.h>

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

/* ---- content-addressed derived-data cache --------------------------- */

/* Point the DDC at a directory. Creates it if needed. Pass NULL to disable. */
int  wb_ddc_init(const char *cache_dir);

/* 1 if a cache entry exists for `src_path` and matches the source's current
 * content hash, 0 otherwise (and if the DDC is disabled). */
int  wb_ddc_is_fresh(const char *src_path);

/* Store a derived model under the source's content hash. Returns 0 on success. */
int  wb_ddc_save(const char *src_path, const wb_model *m);

/* Load a derived model from the cache. Returns 0 on success. */
int  wb_ddc_load(const char *src_path, wb_model *out);

/* The content hash of a file, as 16 hex chars + NUL. Empty if unreadable. */
int  wb_content_hash(const char *path, char out[17]);

/* ---- manifest ------------------------------------------------------ */

typedef struct {
    char *path;         /* asset path */
    char  hash[17];     /* content hash at manifest time */
} wb_manifest_entry;

typedef struct {
    wb_manifest_entry *entries;
    int count;
} wb_manifest;

void wb_manifest_free(wb_manifest *mf);

/* Load a manifest (JSON: {"assets":[{"path":"...","hash":"..."}]}).
 * Returns 0 on success. */
int  wb_manifest_load(const char *path, wb_manifest *out);

/* Write a manifest for the given paths, hashing each. Returns 0 on success. */
int  wb_manifest_write(const char *path, const char **paths, int count);

/* 1 if every manifest entry still matches the file on disk. */
int  wb_manifest_verify(const wb_manifest *mf);

#ifdef __cplusplus
}
#endif

#endif /* WB_ASSETS_H */

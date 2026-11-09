/*
 * wb_assets.c -- content pipeline top level: file/container handling and the
 * module error state. Mesh decoding lives in wb_assets_gltf.c, the derived
 * cache in wb_assets_ddc.c, the manifest in wb_assets_manifest.c.
 */
#include "wb_assets.h"
#include "wb_assets_internal.h"
#include "wb_gltf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char wb_assets_g_err[WB_ASSETS_ERR_SZ];

const char *wb_asset_error(void) {
    return wb_assets_g_err[0] ? wb_assets_g_err : NULL;
}

int wb_asset_load_gltf(const char *path, wb_model *out) {
    wb_assets_g_err[0] = 0;
    if (!path || !out) return -1;
    memset(out, 0, sizeof *out);

    size_t len = 0;
    unsigned char *file = wb_read_file(path, &len);
    if (!file) {
        wb_set_err(wb_assets_g_err, sizeof wb_assets_g_err, "cannot read %s", path);
        return -1;
    }

    const char *json = NULL;
    size_t json_len = 0;
    const unsigned char *bin = NULL;
    size_t bin_len = 0;
    unsigned char *ext_bin = NULL;   /* owned, for the .gltf + .bin path */

    if (wb_gltf_is_glb(file, len)) {
        if (wb_gltf_split_glb(file, len, &json, &json_len, &bin, &bin_len) != 0) {
            wb_set_err(wb_assets_g_err, sizeof wb_assets_g_err,
                       "unsupported GLB in %s", path);
            free(file);
            return -1;
        }
    } else {
        json = (const char *)file;
        json_len = len;
    }

    if (!json) {
        wb_set_err(wb_assets_g_err, sizeof wb_assets_g_err, "no JSON chunk in %s", path);
        free(file);
        return -1;
    }

    wb_json *root = wb_json_parse(json, json_len);
    if (!root) {
        wb_set_err(wb_assets_g_err, sizeof wb_assets_g_err, "JSON parse failed in %s", path);
        free(file);
        return -1;
    }

    /* A .gltf file keeps its buffer in a sibling .bin. */
    if (!bin) {
        wb_json *bufs = wb_json_get(root, "buffers");
        if (bufs && bufs->n > 0) {
            const char *uri = wb_json_str(wb_json_get(bufs->items[0], "uri"));
            if (uri && !strncmp(uri, "data:", 5)) {
                wb_set_err(wb_assets_g_err, sizeof wb_assets_g_err,
                           "data URIs not supported");
                wb_json_free(root);
                free(file);
                return -1;
            } else if (uri) {
                char dir[400];
                snprintf(dir, sizeof dir, "%s", path);
                char *slash = strrchr(dir, '/');
                if (slash) *(slash + 1) = 0; else dir[0] = 0;
                char full[512];
                snprintf(full, sizeof full, "%s%s", dir, uri);
                size_t bl = 0;
                ext_bin = wb_read_file(full, &bl);
                if (ext_bin) { bin = ext_bin; bin_len = bl; }
            }
        }
    }
    if (!bin) { bin = (const unsigned char *)""; bin_len = 0; }

    snprintf(out->source, sizeof out->source, "%s", path);
    wb_json *meshes = wb_json_get(root, "meshes");
    if (meshes) {
        for (int i = 0; i < meshes->n; i++)
            wb_assets_load_mesh(out, root, meshes->items[i], i, bin, bin_len);
    }

    int rc = out->nmeshes > 0 ? 0 : -1;
    if (rc != 0)
        wb_set_err(wb_assets_g_err, sizeof wb_assets_g_err, "no meshes loaded from %s", path);

    wb_json_free(root);
    free(ext_bin);
    free(file);
    return rc;
}

void wb_model_free(wb_model *m) {
    if (!m) return;
    for (int i = 0; i < m->nmeshes; i++) {
        free(m->meshes[i].verts);
        free(m->meshes[i].indices);
    }
    free(m->meshes);
    m->meshes = NULL;
    m->nmeshes = 0;
}

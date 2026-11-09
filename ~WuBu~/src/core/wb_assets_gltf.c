/*
 * wb_assets_gltf.c -- glTF mesh decoding: primitives -> engine vertex/index
 * arrays, plus material factors. Accessor resolution is shared (wb_gltf).
 */
#include "wb_assets_internal.h"
#include "wb_gltf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void wb_assets_load_mesh(wb_model *m, const wb_json *root, const wb_json *mesh,
                         int mesh_index, const unsigned char *bin,
                         size_t bin_len) {
    wb_json *prims = wb_json_get(mesh, "primitives");
    if (!prims) return;
    for (int pi = 0; pi < prims->n; pi++) {
        wb_json *prim = prims->items[pi];
        wb_json *attrs = wb_json_get(prim, "attributes");
        if (!attrs) continue;

        int pos_i = wb_json_int(wb_json_get(attrs, "POSITION"), -1);
        int nrm_i = wb_json_int(wb_json_get(attrs, "NORMAL"), -1);
        int idx_i = wb_json_int(wb_json_get(prim, "indices"), -1);
        if (pos_i < 0) continue;

        wb_gltf_accessor pa, na, ia;
        const unsigned char *pd = wb_gltf_accessor_bytes(root, bin, bin_len, pos_i, &pa);
        if (!pd) continue;
        const unsigned char *nd = NULL;
        if (nrm_i >= 0) nd = wb_gltf_accessor_bytes(root, bin, bin_len, nrm_i, &na);
        const unsigned char *id = NULL;
        if (idx_i >= 0) id = wb_gltf_accessor_bytes(root, bin, bin_len, idx_i, &ia);

        m->meshes = (wb_mesh *)realloc(m->meshes, (size_t)(m->nmeshes + 1) * sizeof(wb_mesh));
        if (!m->meshes) return;
        wb_mesh *out = &m->meshes[m->nmeshes];
        memset(out, 0, sizeof *out);
        snprintf(out->name, sizeof out->name, "mesh_%d", mesh_index);
        out->base_color[0] = out->base_color[1] = out->base_color[2] = 0.8f;
        out->base_color[3] = 1.0f;
        out->metallic = 0.0f;
        out->roughness = 0.6f;

        out->nverts = pa.count;
        out->verts = (wb_vertex *)calloc((size_t)pa.count, sizeof(wb_vertex));
        if (!out->verts) return;
        for (int i = 0; i < pa.count; i++) {
            const unsigned char *src = pa.data + (size_t)i * (size_t)pa.byte_stride;
            for (int c = 0; c < 3; c++)
                out->verts[i].pos[c] = wb_gltf_read_comp(src, pa.component_type, c);
            if (nd) {
                const unsigned char *ns = na.data + (size_t)i * (size_t)na.byte_stride;
                for (int c = 0; c < 3; c++)
                    out->verts[i].normal[c] = wb_gltf_read_comp(ns, na.component_type, c);
            } else {
                out->verts[i].normal[2] = 1.0f;
            }
        }

        if (id) {
            out->nindices = ia.count;
            out->indices = (uint32_t *)calloc((size_t)ia.count, sizeof(uint32_t));
            if (out->indices)
                for (int i = 0; i < ia.count; i++)
                    out->indices[i] = (uint32_t)wb_gltf_read_comp(ia.data, ia.component_type, i);
        }

        /* material (optional) */
        int mat_i = wb_json_int(wb_json_get(prim, "material"), -1);
        if (mat_i >= 0) {
            wb_json *mats = wb_json_get(root, "materials");
            if (mats && mat_i < mats->n) {
                wb_json *mr = wb_json_get(mats->items[mat_i], "pbrMetallicRoughness");
                if (mr) {
                    wb_json *bc = wb_json_get(mr, "baseColorFactor");
                    if (bc && bc->type == WB_JSON_ARR && bc->n >= 3) {
                        for (int c = 0; c < 4; c++)
                            if (c < bc->n) out->base_color[c] = (float)bc->items[c]->num;
                    }
                    out->metallic  = (float)wb_json_num(wb_json_get(mr, "metallicFactor"), 0.0);
                    out->roughness = (float)wb_json_num(wb_json_get(mr, "roughnessFactor"), 0.6);
                }
            }
        }
        m->nmeshes++;
    }
}

/*
 * wb_assets_ddc.c -- content-addressed derived-data cache. See wb_ddc.h.
 */
#include "wb_ddc.h"
#include "wb_assets_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_ddc_dir[512] = "";

static void ddc_path(const char *src_path, char *out, size_t out_sz) {
    char h[17];
    wb_content_hash(src_path, h);
    snprintf(out, out_sz, "%s/%s.wbm", g_ddc_dir, h);
}

int wb_ddc_init(const char *cache_dir) {
    if (!cache_dir || !cache_dir[0]) { g_ddc_dir[0] = 0; return 0; }
    snprintf(g_ddc_dir, sizeof g_ddc_dir, "%s", cache_dir);
    if (wb_ensure_dir(cache_dir) != 0) {
        wb_set_err(wb_assets_g_err, sizeof wb_assets_g_err,
                   "cannot create DDC dir %s", cache_dir);
        return -1;
    }
    return 0;
}

int wb_ddc_is_fresh(const char *src_path) {
    if (!g_ddc_dir[0] || !src_path) return 0;
    char p[600];
    ddc_path(src_path, p, sizeof p);
    return wb_file_exists(p);
}

int wb_ddc_save(const char *src_path, const wb_model *m) {
    if (!g_ddc_dir[0] || !src_path || !m) return -1;
    char p[600];
    ddc_path(src_path, p, sizeof p);
    FILE *f = fopen(p, "wb");
    if (!f) {
        wb_set_err(wb_assets_g_err, sizeof wb_assets_g_err, "cannot write DDC %s", p);
        return -1;
    }
    fwrite("WBM1", 1, 4, f);
    int32_t nm = (int32_t)m->nmeshes;
    fwrite(&nm, sizeof nm, 1, f);
    for (int i = 0; i < m->nmeshes; i++) {
        const wb_mesh *mm = &m->meshes[i];
        int32_t nv = mm->nverts, ni = mm->nindices;
        fwrite(&nv, sizeof nv, 1, f);
        fwrite(&ni, sizeof ni, 1, f);
        fwrite(mm->base_color, sizeof(float), 4, f);
        float mr[2] = { mm->metallic, mm->roughness };
        fwrite(mr, sizeof(float), 2, f);
        if (nv) fwrite(mm->verts, sizeof(wb_vertex), (size_t)nv, f);
        if (ni) fwrite(mm->indices, sizeof(uint32_t), (size_t)ni, f);
    }
    fclose(f);
    return 0;
}

int wb_ddc_load(const char *src_path, wb_model *out) {
    if (!g_ddc_dir[0] || !src_path || !out) return -1;
    char p[600];
    ddc_path(src_path, p, sizeof p);
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    memset(out, 0, sizeof *out);
    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "WBM1", 4) != 0) {
        fclose(f);
        wb_set_err(wb_assets_g_err, sizeof wb_assets_g_err, "bad DDC magic in %s", p);
        return -1;
    }
    int32_t nm = 0;
    if (fread(&nm, sizeof nm, 1, f) != 1 || nm < 0 || nm > 100000) {
        fclose(f);
        wb_set_err(wb_assets_g_err, sizeof wb_assets_g_err, "bad DDC mesh count");
        return -1;
    }
    out->meshes = (wb_mesh *)calloc((size_t)nm, sizeof(wb_mesh));
    if (!out->meshes) { fclose(f); return -1; }
    for (int i = 0; i < nm; i++) {
        wb_mesh *mm = &out->meshes[i];
        int32_t nv = 0, ni = 0;
        if (fread(&nv, sizeof nv, 1, f) != 1 || fread(&ni, sizeof ni, 1, f) != 1) break;
        if (fread(mm->base_color, sizeof(float), 4, f) != 4) break;
        float mr[2];
        if (fread(mr, sizeof(float), 2, f) != 2) break;
        mm->metallic = mr[0]; mm->roughness = mr[1];
        mm->nverts = nv; mm->nindices = ni;
        if (nv > 0) {
            mm->verts = (wb_vertex *)calloc((size_t)nv, sizeof(wb_vertex));
            if (fread(mm->verts, sizeof(wb_vertex), (size_t)nv, f) != (size_t)nv) break;
        }
        if (ni > 0) {
            mm->indices = (uint32_t *)calloc((size_t)ni, sizeof(uint32_t));
            if (fread(mm->indices, sizeof(uint32_t), (size_t)ni, f) != (size_t)ni) break;
        }
        out->nmeshes++;
    }
    fclose(f);
    snprintf(out->source, sizeof out->source, "%s", src_path);
    return 0;
}

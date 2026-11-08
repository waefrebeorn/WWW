/*
 * wb_assets.c -- WuBu Engine content pipeline (slice 12). See wb_assets.h.
 */
#include "wb_assets.h"
#include "wb_json.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

/* ==================================================================== */
/* small utilities                                                      */
/* ==================================================================== */

static char g_err[512];

const char *wb_asset_error(void) { return g_err[0] ? g_err : NULL; }

static void set_err(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_err, sizeof g_err, fmt, ap);
    va_end(ap);
}

static unsigned char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    unsigned char *b = (unsigned char *)malloc((size_t)n + 1);
    if (!b) { fclose(f); return NULL; }
    size_t got = fread(b, 1, (size_t)n, f);
    fclose(f);
    b[got] = 0;
    *out_len = got;
    return b;
}

static int file_exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0;
}

static int ensure_dir(const char *path) {
    /* create one directory level (the DDC root) */
    if (file_exists(path)) return 0;
    #ifdef _WIN32
    return _mkdir(path) == 0 ? 0 : -1;
    #else
    return mkdir(path, 0755) == 0 ? 0 : -1;
    #endif
}

/* ---- content hash: FNV-1a 64-bit ---------------------------------- */

static uint64_t fnv1a(const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < len; i++) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

int wb_content_hash(const char *path, char out[17]) {
    out[0] = 0;
    size_t len = 0;
    unsigned char *b = read_file(path, &len);
    if (!b) return -1;
    uint64_t h = fnv1a(b, len);
    free(b);
    snprintf(out, 17, "%016llx", (unsigned long long)h);
    return 0;
}

/* ==================================================================== */
/* glTF / GLB loading                                                   */
/* ==================================================================== */

typedef struct {
    const unsigned char *data;
    size_t len;
    size_t byte_offset;
    int    byte_stride;
    int    component_type;
    int    count;
    int    type;   /* 1=SCALAR, 2=VEC2, 3=VEC3, 4=VEC4 */
} gltf_accessor;

static int comp_size(int ct) {
    switch (ct) {
        case 5120: case 5121: return 1;
        case 5122: case 5123: return 2;
        case 5125: case 5126: return 4;
    }
    return 0;
}

static int type_comps(int t) {
    switch (t) {
        case 1: return 1;   /* SCALAR */
        case 2: return 2;   /* VEC2 */
        case 3: return 3;   /* VEC3 */
        case 4: return 4;   /* VEC4 */
    }
    return 0;
}

static float read_comp(const unsigned char *base, int ct, int idx) {
    switch (ct) {
        case 5120: return (float)((const signed char *)base)[idx];
        case 5121: return (float)((const unsigned char *)base)[idx];
        case 5122: return (float)((const short *)base)[idx];
        case 5123: return (float)((const unsigned short *)base)[idx];
        case 5125: return (float)((const unsigned int *)base)[idx];
        case 5126: return ((const float *)base)[idx];
    }
    return 0.0f;
}

/* Resolve an accessor index to its bytes. Returns NULL on failure. */
static const unsigned char *accessor_bytes(const wb_json *root, const unsigned char *bin, size_t bin_len,
                                           int acc_index, gltf_accessor *out) {
    wb_json *accs = wb_json_get(root, "accessors");
    if (!accs || acc_index < 0 || acc_index >= accs->n) return NULL;
    wb_json *a = accs->items[acc_index];
    int bv = wb_json_int(wb_json_get(a, "bufferView"), -1);
    if (bv < 0) return NULL;
    wb_json *bvs = wb_json_get(root, "bufferViews");
    if (!bvs || bv >= bvs->n) return NULL;
    wb_json *b = bvs->items[bv];

    int offset = wb_json_int(wb_json_get(a, "byteOffset"), 0);
    int bvoff = wb_json_int(wb_json_get(b, "byteOffset"), 0);
    int ct = wb_json_int(wb_json_get(a, "componentType"), 0);
    int count = wb_json_int(wb_json_get(a, "count"), 0);
    const char *ty = wb_json_str(wb_json_get(a, "type"));
    int t = 1;
    if (ty) {
        if (!strcmp(ty, "SCALAR")) t = 1;
        else if (!strcmp(ty, "VEC2")) t = 2;
        else if (!strcmp(ty, "VEC3")) t = 3;
        else if (!strcmp(ty, "VEC4")) t = 4;
    }
    size_t need = (size_t)bvoff + (size_t)offset +
                  (size_t)count * (size_t)type_comps(t) * (size_t)comp_size(ct);
    if (need > bin_len) return NULL;

    out->data = bin + bvoff + offset;
    out->len = (size_t)count * (size_t)type_comps(t) * (size_t)comp_size(ct);
    out->component_type = ct;
    out->count = count;
    out->type = t;
    out->byte_stride = wb_json_int(wb_json_get(b, "byteStride"), 0);
    /* a missing byteStride means tightly packed, NOT zero-stride */
    if (out->byte_stride == 0)
        out->byte_stride = comp_size(ct) * type_comps(t);
    return out->data;
}

static void load_mesh(wb_model *m, const wb_json *root, const wb_json *mesh, int mesh_index,
                      const unsigned char *bin, size_t bin_len) {
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

        gltf_accessor pa, na, ia;
        const unsigned char *pd = accessor_bytes(root, bin, bin_len, pos_i, &pa);
        if (!pd) continue;
        const unsigned char *nd = NULL;
        if (nrm_i >= 0) nd = accessor_bytes(root, bin, bin_len, nrm_i, &na);
        const unsigned char *id = NULL;
        if (idx_i >= 0) id = accessor_bytes(root, bin, bin_len, idx_i, &ia);

        m->meshes = (wb_mesh *)realloc(m->meshes, (size_t)(m->nmeshes + 1) * sizeof(wb_mesh));
        wb_mesh *out = &m->meshes[m->nmeshes];
        memset(out, 0, sizeof *out);
        snprintf(out->name, sizeof out->name, "mesh_%d", mesh_index);
        out->base_color[0] = out->base_color[1] = out->base_color[2] = 0.8f;
        out->base_color[3] = 1.0f;
        out->metallic = 0.0f;
        out->roughness = 0.6f;

        out->nverts = pa.count;
        out->verts = (wb_vertex *)calloc((size_t)pa.count, sizeof(wb_vertex));
        for (int i = 0; i < pa.count; i++) {
            const unsigned char *src = pa.data + (size_t)i * pa.byte_stride;
            for (int c = 0; c < 3; c++) out->verts[i].pos[c] = read_comp(src, pa.component_type, c);
            if (nd) {
                const unsigned char *ns = na.data + (size_t)i * na.byte_stride;
                for (int c = 0; c < 3; c++) out->verts[i].normal[c] = read_comp(ns, na.component_type, c);
            } else {
                out->verts[i].normal[2] = 1.0f;
            }
        }

        if (id) {
            out->nindices = ia.count;
            out->indices = (uint32_t *)calloc((size_t)ia.count, sizeof(uint32_t));
            for (int i = 0; i < ia.count; i++)
                out->indices[i] = (uint32_t)read_comp(ia.data, ia.component_type, i);
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

int wb_asset_load_gltf(const char *path, wb_model *out) {
    g_err[0] = 0;
    if (!path || !out) return -1;
    memset(out, 0, sizeof *out);

    size_t len = 0;
    unsigned char *file = read_file(path, &len);
    if (!file) { set_err("cannot read %s", path); return -1; }

    unsigned char *bin = NULL;
    size_t bin_len = 0;
    const char *json = NULL;
    size_t json_len = 0;

    if (len >= 12 && !memcmp(file, "glTF", 4)) {
        /* GLB: header + chunks */
        unsigned version = (unsigned)file[4] | ((unsigned)file[5] << 8) |
                           ((unsigned)file[6] << 16) | ((unsigned)file[7] << 24);
        if (version != 2) { set_err("unsupported GLB version %u", version); free(file); return -1; }
        size_t off = 12;
        while (off + 8 <= len) {
            unsigned clen = (unsigned)file[off] | ((unsigned)file[off+1] << 8) |
                            ((unsigned)file[off+2] << 16) | ((unsigned)file[off+3] << 24);
            unsigned ctype = (unsigned)file[off+4] | ((unsigned)file[off+5] << 8) |
                             ((unsigned)file[off+6] << 16) | ((unsigned)file[off+7] << 24);
            off += 8;
            if (off + clen > len) break;
            if (ctype == 0x4E4F534A) { json = (const char *)file + off; json_len = clen; }
            else if (ctype == 0x004E4942) { bin = file + off; bin_len = clen; }
            off += clen;
        }
    } else {
        json = (const char *)file;
        json_len = len;
    }

    if (!json) { set_err("no JSON chunk in %s", path); free(file); return -1; }

    wb_json *root = wb_json_parse(json, json_len);
    if (!root) { set_err("JSON parse failed in %s", path); free(file); return -1; }

    /* external .bin for .gltf files */
    if (!bin) {
        wb_json *bufs = wb_json_get(root, "buffers");
        if (bufs && bufs->n > 0) {
            const char *uri = wb_json_str(wb_json_get(bufs->items[0], "uri"));
            if (uri && !strncmp(uri, "data:", 5)) {
                /* data URI: base64 -- not supported in this slice */
                set_err("data URIs not supported in this slice");
                wb_json_free(root); free(file); return -1;
            } else if (uri) {
                char dir[400];
                snprintf(dir, sizeof dir, "%s", path);
                char *slash = strrchr(dir, '/');
                if (slash) *(slash + 1) = 0; else strcpy(dir, "");
                char full[512];
                snprintf(full, sizeof full, "%s%s", dir, uri);
                size_t bl = 0;
                unsigned char *bb = read_file(full, &bl);
                if (bb) { bin = bb; bin_len = bl; }
            }
        }
    }
    if (!bin) { bin = (unsigned char *)""; bin_len = 0; }

    snprintf(out->source, sizeof out->source, "%s", path);
    wb_json *meshes = wb_json_get(root, "meshes");
    if (meshes) {
        for (int i = 0; i < meshes->n; i++)
            load_mesh(out, root, meshes->items[i], i, bin, bin_len);
    }

    if (out->nmeshes == 0) set_err("no meshes loaded from %s", path);
    int rc = out->nmeshes > 0 ? 0 : -1;

    wb_json_free(root);
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

/* ==================================================================== */
/* content-addressed derived-data cache                                 */
/* ==================================================================== */

static char g_ddc_dir[512] = "";

int wb_ddc_init(const char *cache_dir) {
    if (!cache_dir || !cache_dir[0]) { g_ddc_dir[0] = 0; return 0; }
    snprintf(g_ddc_dir, sizeof g_ddc_dir, "%s", cache_dir);
    if (ensure_dir(cache_dir) != 0) {
        set_err("cannot create DDC dir %s", cache_dir);
        return -1;
    }
    return 0;
}

static void ddc_path(const char *src_path, char *out, size_t out_sz) {
    char h[17];
    wb_content_hash(src_path, h);
    snprintf(out, out_sz, "%s/%s.wbm", g_ddc_dir, h);
}

int wb_ddc_is_fresh(const char *src_path) {
    if (!g_ddc_dir[0] || !src_path) return 0;
    char p[600];
    ddc_path(src_path, p, sizeof p);
    return file_exists(p);
}

int wb_ddc_save(const char *src_path, const wb_model *m) {
    if (!g_ddc_dir[0] || !src_path || !m) return -1;
    char p[600];
    ddc_path(src_path, p, sizeof p);
    FILE *f = fopen(p, "wb");
    if (!f) { set_err("cannot write DDC %s", p); return -1; }
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
        fclose(f); set_err("bad DDC magic in %s", p); return -1;
    }
    int32_t nm = 0;
    if (fread(&nm, sizeof nm, 1, f) != 1 || nm < 0 || nm > 100000) {
        fclose(f); set_err("bad DDC mesh count"); return -1;
    }
    out->meshes = (wb_mesh *)calloc((size_t)nm, sizeof(wb_mesh));
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

/* ==================================================================== */
/* manifest                                                             */
/* ==================================================================== */

void wb_manifest_free(wb_manifest *mf) {
    if (!mf) return;
    for (int i = 0; i < mf->count; i++) free(mf->entries[i].path);
    free(mf->entries);
    mf->entries = NULL;
    mf->count = 0;
}

int wb_manifest_load(const char *path, wb_manifest *out) {
    g_err[0] = 0;
    if (!path || !out) return -1;
    memset(out, 0, sizeof *out);
    size_t len = 0;
    unsigned char *file = read_file(path, &len);
    if (!file) { set_err("cannot read manifest %s", path); return -1; }
    wb_json *root = wb_json_parse((const char *)file, len);
    free(file);
    if (!root) { set_err("manifest JSON parse failed"); return -1; }
    wb_json *assets = wb_json_get(root, "assets");
    if (!assets || assets->type != WB_JSON_ARR) { wb_json_free(root); set_err("manifest has no assets array"); return -1; }
    out->entries = (wb_manifest_entry *)calloc((size_t)assets->n, sizeof(wb_manifest_entry));
    for (int i = 0; i < assets->n; i++) {
        const char *p = wb_json_str(wb_json_get(assets->items[i], "path"));
        const char *h = wb_json_str(wb_json_get(assets->items[i], "hash"));
        if (!p) continue;
        out->entries[out->count].path = strdup(p);
        snprintf(out->entries[out->count].hash, 17, "%s", h ? h : "");
        out->count++;
    }
    wb_json_free(root);
    return 0;
}

int wb_manifest_write(const char *path, const char **paths, int count) {
    if (!path || !paths) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) { set_err("cannot write manifest %s", path); return -1; }
    fprintf(f, "{\n  \"assets\": [\n");
    for (int i = 0; i < count; i++) {
        char h[17] = "";
        wb_content_hash(paths[i], h);
        fprintf(f, "    {\"path\": \"%s\", \"hash\": \"%s\"}%s\n",
                paths[i], h, (i + 1 < count) ? "," : "");
    }
    fprintf(f, "  ]\n}\n");
    fclose(f);
    return 0;
}

int wb_manifest_verify(const wb_manifest *mf) {
    if (!mf) return 0;
    for (int i = 0; i < mf->count; i++) {
        char h[17];
        if (wb_content_hash(mf->entries[i].path, h) != 0) return 0;
        if (strcmp(h, mf->entries[i].hash) != 0) return 0;
    }
    return 1;
}

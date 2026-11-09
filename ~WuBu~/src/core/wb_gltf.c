/*
 * wb_gltf.c -- WuBu Engine shared glTF accessor decoding. See wb_gltf.h.
 */
#include "wb_gltf.h"

#include <string.h>

int wb_gltf_comp_size(int ct) {
    switch (ct) {
        case 5120: case 5121: return 1;   /* BYTE, UNSIGNED_BYTE */
        case 5122: case 5123: return 2;   /* SHORT, UNSIGNED_SHORT */
        case 5125: case 5126: return 4;   /* UNSIGNED_INT, FLOAT */
    }
    return 0;
}

int wb_gltf_type_comps(int t) {
    switch (t) {
        case 1: return 1;    /* SCALAR */
        case 2: return 2;    /* VEC2 */
        case 3: return 3;    /* VEC3 */
        case 4: return 4;    /* VEC4 */
        case 16: return 16;  /* MAT4 */
    }
    return 0;
}

float wb_gltf_read_comp(const unsigned char *base, int ct, int idx) {
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

const unsigned char *wb_gltf_accessor_bytes(const wb_json *root,
                                            const unsigned char *bin,
                                            size_t bin_len, int acc_index,
                                            wb_gltf_accessor *out) {
    if (!root || !out) return NULL;
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
    int type = 1;
    if (ty) {
        if (!strcmp(ty, "SCALAR")) type = 1;
        else if (!strcmp(ty, "VEC2")) type = 2;
        else if (!strcmp(ty, "VEC3")) type = 3;
        else if (!strcmp(ty, "VEC4")) type = 4;
        else if (!strcmp(ty, "MAT4")) type = 16;
    }
    int comps = wb_gltf_type_comps(type);
    int cs = wb_gltf_comp_size(ct);
    if (cs == 0 || comps == 0) return NULL;

    size_t need = (size_t)bvoff + (size_t)offset +
                  (size_t)count * (size_t)comps * (size_t)cs;
    if (need > bin_len) return NULL;

    out->data = bin + bvoff + offset;
    out->len = (size_t)count * (size_t)comps * (size_t)cs;
    out->component_type = ct;
    out->count = count;
    out->type = type;
    out->comps = comps;
    out->byte_stride = wb_json_int(wb_json_get(b, "byteStride"), 0);
    /* a missing byteStride means tightly packed, NOT zero-stride */
    if (out->byte_stride == 0) out->byte_stride = cs * comps;
    return out->data;
}

int wb_gltf_read_floats(const wb_gltf_accessor *a, int index, int comps,
                        float *out) {
    if (!a || !out) return -1;
    if (index < 0 || index >= a->count) return -1;
    const unsigned char *src = a->data + (size_t)index * (size_t)a->byte_stride;
    for (int c = 0; c < comps; c++) out[c] = wb_gltf_read_comp(src, a->component_type, c);
    return 0;
}

int wb_gltf_is_glb(const unsigned char *file, size_t len) {
    return file && len >= 12 && memcmp(file, "glTF", 4) == 0;
}

int wb_gltf_split_glb(const unsigned char *file, size_t len,
                      const char **json, size_t *json_len,
                      const unsigned char **bin, size_t *bin_len) {
    if (json) *json = NULL;
    if (json_len) *json_len = 0;
    if (bin) *bin = NULL;
    if (bin_len) *bin_len = 0;
    if (!file || len < 12 || memcmp(file, "glTF", 4) != 0) return -1;

    unsigned version = (unsigned)file[4] | ((unsigned)file[5] << 8) |
                       ((unsigned)file[6] << 16) | ((unsigned)file[7] << 24);
    if (version != 2) return -1;

    size_t off = 12;
    while (off + 8 <= len) {
        unsigned clen = (unsigned)file[off] | ((unsigned)file[off + 1] << 8) |
                        ((unsigned)file[off + 2] << 16) | ((unsigned)file[off + 3] << 24);
        unsigned ctype = (unsigned)file[off + 4] | ((unsigned)file[off + 5] << 8) |
                         ((unsigned)file[off + 6] << 16) | ((unsigned)file[off + 7] << 24);
        off += 8;
        if (off + clen > len) break;
        if (ctype == 0x4E4F534A) { if (json) *json = (const char *)file + off; if (json_len) *json_len = clen; }
        else if (ctype == 0x004E4942) { if (bin) *bin = file + off; if (bin_len) *bin_len = clen; }
        off += clen;
    }
    return 0;
}

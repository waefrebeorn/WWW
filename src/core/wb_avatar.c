/*
 * wb_avatar.c -- WuBu Engine avatar + identity (slice 14). See wb_avatar.h.
 */
#include "wb_avatar.h"
#include "wb_json.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

static char g_err[512];

const char *wb_avatar_error(void) { return g_err[0] ? g_err : NULL; }

static void set_err(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_err, sizeof g_err, fmt, ap);
    va_end(ap);
}

/* ==================================================================== */
/* bone name mapping                                                     */
/* ==================================================================== */

static const char *kBoneNames[WB_BONE_COUNT] = {
    "hips", "spine", "chest", "head",
    "left_upper_arm", "left_lower_arm", "left_hand",
    "right_upper_arm", "right_lower_arm", "right_hand",
    "left_upper_leg", "left_lower_leg", "left_foot",
    "right_upper_leg", "right_lower_leg", "right_foot"
};

const char *wb_bone_name(int bone) {
    return (bone >= 0 && bone < WB_BONE_COUNT) ? kBoneNames[bone] : "?";
}

static void lower_copy(const char *src, char *dst, size_t dst_sz) {
    size_t i = 0;
    for (; src[i] && i + 1 < dst_sz; i++) {
        char c = src[i];
        dst[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    dst[i] = 0;
}

int wb_bone_from_name(const char *name) {
    if (!name) return -1;
    char low[128];
    lower_copy(name, low, sizeof low);

    /* Longest / most specific first so "left_upper_arm" is not eaten by
     * a bare "arm" test. */
    struct { const char *needle; int bone; } table[] = {
        /* Mixamo / VRM style names have no underscores, so match both forms. */
        { "leftupperarm", WB_BONE_LEFT_UPPER_ARM },
        { "leftuparm",    WB_BONE_LEFT_UPPER_ARM },
        { "leftarm",      WB_BONE_LEFT_UPPER_ARM },
        { "leftforearm",  WB_BONE_LEFT_LOWER_ARM },
        { "lefthand",     WB_BONE_LEFT_HAND },
        { "rightupperarm", WB_BONE_RIGHT_UPPER_ARM },
        { "rightuparm",   WB_BONE_RIGHT_UPPER_ARM },
        { "rightarm",     WB_BONE_RIGHT_UPPER_ARM },
        { "rightforearm", WB_BONE_RIGHT_LOWER_ARM },
        { "righthand",    WB_BONE_RIGHT_HAND },
        { "leftupperleg", WB_BONE_LEFT_UPPER_LEG },
        { "leftupleg",    WB_BONE_LEFT_UPPER_LEG },
        { "leftleg",      WB_BONE_LEFT_UPPER_LEG },
        { "leftfoot",     WB_BONE_LEFT_FOOT },
        { "rightupperleg", WB_BONE_RIGHT_UPPER_LEG },
        { "rightupleg",   WB_BONE_RIGHT_UPPER_LEG },
        { "rightleg",     WB_BONE_RIGHT_UPPER_LEG },
        { "rightfoot",    WB_BONE_RIGHT_FOOT },
        { "hips",            WB_BONE_HIPS },
        { "pelvis",          WB_BONE_HIPS },
        { "spine",           WB_BONE_SPINE },
        { "chest",           WB_BONE_CHEST },
        { "neck",            WB_BONE_HEAD },
        { "head",            WB_BONE_HEAD },
    };
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++)
        if (strstr(low, table[i].needle)) return table[i].bone;
    return -1;
}

/* ==================================================================== */
/* glTF accessor reading (re-used for skins and animations)              */
/* ==================================================================== */

typedef struct {
    const unsigned char *data;
    size_t len;
    int component_type;
    int count;
    int comps;
    int byte_stride;
} av_accessor;

static int comp_size(int ct) {
    switch (ct) {
        case 5120: case 5121: return 1;
        case 5122: case 5123: return 2;
        case 5125: case 5126: return 4;
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

static const unsigned char *accessor_bytes(const wb_json *root,
                                           const unsigned char *bin, size_t bin_len,
                                           int acc_index, av_accessor *out) {
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
    int comps = 1;
    if (ty) {
        if (!strcmp(ty, "SCALAR")) comps = 1;
        else if (!strcmp(ty, "VEC2")) comps = 2;
        else if (!strcmp(ty, "VEC3")) comps = 3;
        else if (!strcmp(ty, "VEC4")) comps = 4;
        else if (!strcmp(ty, "MAT4")) comps = 16;
    }
    size_t need = (size_t)bvoff + (size_t)offset +
                  (size_t)count * (size_t)comps * (size_t)comp_size(ct);
    if (need > bin_len) return NULL;

    out->data = bin + bvoff + offset;
    out->len = (size_t)count * (size_t)comps * (size_t)comp_size(ct);
    out->component_type = ct;
    out->count = count;
    out->comps = comps;
    out->byte_stride = wb_json_int(wb_json_get(b, "byteStride"), 0);
    if (out->byte_stride == 0) out->byte_stride = comp_size(ct) * comps;
    return out->data;
}

/* Read `count` elements of `comps` floats each into `out` (row-major). */
static int read_floats(const av_accessor *a, int index, int comps, float *out) {
    if (index < 0 || index >= a->count) return -1;
    const unsigned char *src = a->data + (size_t)index * a->byte_stride;
    for (int c = 0; c < comps; c++) out[c] = read_comp(src, a->component_type, c);
    return 0;
}

/* ==================================================================== */
/* loading                                                              */
/* ==================================================================== */

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

static void load_rig(const wb_json *root, const unsigned char *bin, size_t bin_len,
                     wb_rig *rig) {
    memset(rig, 0, sizeof *rig);
    for (int i = 0; i < WB_BONE_COUNT; i++) { rig->bones[i].node = -1; rig->bones[i].parent = -1; }

    wb_json *skins = wb_json_get(root, "skins");
    if (!skins || skins->n == 0) return;
    wb_json *skin = skins->items[0];

    wb_json *joints = wb_json_get(skin, "joints");
    if (!joints) return;

    int ibm = wb_json_int(wb_json_get(skin, "inverseBindMatrices"), -1);
    av_accessor ibm_acc;
    const unsigned char *ibm_data = NULL;
    if (ibm >= 0) ibm_data = accessor_bytes(root, bin, bin_len, ibm, &ibm_acc);

    rig->njoints = joints->n < WB_AVATAR_MAX_JOINTS ? joints->n : WB_AVATAR_MAX_JOINTS;
    for (int i = 0; i < rig->njoints; i++) {
        int node = wb_json_int(joints->items[i], 0);
        rig->joint_nodes[i] = node;
        if (ibm_data && ibm_acc.comps == 16) {
            float m[16];
            if (read_floats(&ibm_acc, i, 16, m) == 0)
                memcpy(rig->inv_bind[i], m, sizeof m);
        }
    }

    /* Map joint nodes onto the humanoid bone set by node name. */
    wb_json *nodes = wb_json_get(root, "nodes");
    for (int i = 0; i < rig->njoints; i++) {
        int node = rig->joint_nodes[i];
        if (!nodes || node < 0 || node >= nodes->n) continue;
        const char *name = wb_json_str(wb_json_get(nodes->items[node], "name"));
        int bone = wb_bone_from_name(name);
        if (bone < 0) continue;
        rig->bones[bone].node = node;
        snprintf(rig->bones[bone].name, sizeof rig->bones[bone].name, "%s", name ? name : "");
        rig->nmapped++;
    }

    /* Parent from the glTF node hierarchy (child -> parent). */
    for (int b = 0; b < WB_BONE_COUNT; b++) {
        if (rig->bones[b].node < 0) continue;
        int node = rig->bones[b].node;
        if (!nodes || node >= nodes->n) continue;
        wb_json *children = wb_json_get(nodes->items[node], "children");
        if (!children) continue;
        for (int c = 0; c < children->n; c++) {
            int child_node = wb_json_int(children->items[c], -1);
            for (int cb = 0; cb < WB_BONE_COUNT; cb++) {
                if (rig->bones[cb].node == child_node) rig->bones[cb].parent = b;
            }
        }
    }
}

static void load_animation(const wb_json *root, const unsigned char *bin, size_t bin_len,
                           const wb_json *anim, wb_animation *out) {
    memset(out, 0, sizeof *out);
    const char *name = wb_json_str(wb_json_get(anim, "name"));
    snprintf(out->name, sizeof out->name, "%s", name ? name : "clip");

    /* Start every node at its rest TRS so an animation that only drives, say,
     * rotation still preserves the node's authored translation and scale. */
    wb_json *nodes = wb_json_get(root, "nodes");
    if (nodes) {
        for (int i = 0; i < nodes->n && i < 256; i++) {
            wb_trs *r = &out->rest[i];
            r->t[0] = r->t[1] = r->t[2] = 0.0f;
            r->q[0] = r->q[1] = r->q[2] = 0.0f; r->q[3] = 1.0f;
            r->s[0] = r->s[1] = r->s[2] = 1.0f;
            wb_json *t = wb_json_get(nodes->items[i], "translation");
            if (t && t->type == WB_JSON_ARR && t->n >= 3) {
                r->t[0] = (float)t->items[0]->num;
                r->t[1] = (float)t->items[1]->num;
                r->t[2] = (float)t->items[2]->num;
            }
            wb_json *q = wb_json_get(nodes->items[i], "rotation");
            if (q && q->type == WB_JSON_ARR && q->n >= 4) {
                r->q[0] = (float)q->items[0]->num;
                r->q[1] = (float)q->items[1]->num;
                r->q[2] = (float)q->items[2]->num;
                r->q[3] = (float)q->items[3]->num;
            }
            wb_json *s = wb_json_get(nodes->items[i], "scale");
            if (s && s->type == WB_JSON_ARR && s->n >= 3) {
                r->s[0] = (float)s->items[0]->num;
                r->s[1] = (float)s->items[1]->num;
                r->s[2] = (float)s->items[2]->num;
            }
        }
    }

    wb_json *samplers = wb_json_get(anim, "samplers");
    wb_json *channels = wb_json_get(anim, "channels");
    if (!channels) return;

    for (int ci = 0; ci < channels->n && out->nchannels < WB_AVATAR_MAX_CHAN; ci++) {
        wb_json *ch = channels->items[ci];
        int sampler_i = wb_json_int(wb_json_get(ch, "sampler"), -1);
        wb_json *target = wb_json_get(ch, "target");
        if (!target || sampler_i < 0 || !samplers || sampler_i >= samplers->n) continue;
        wb_json *sampler = samplers->items[sampler_i];
        int input_i = wb_json_int(wb_json_get(sampler, "input"), -1);
        int output_i = wb_json_int(wb_json_get(sampler, "output"), -1);
        const char *path = wb_json_str(wb_json_get(target, "path"));
        int node = wb_json_int(wb_json_get(target, "node"), -1);
        if (input_i < 0 || output_i < 0 || !path) continue;

        av_accessor times, values;
        if (!accessor_bytes(root, bin, bin_len, input_i, &times)) continue;
        if (!accessor_bytes(root, bin, bin_len, output_i, &values)) continue;

        wb_anim_channel *oc = &out->channels[out->nchannels];
        memset(oc, 0, sizeof *oc);
        oc->node = node;
        oc->path = !strcmp(path, "translation") ? 0 : (!strcmp(path, "rotation") ? 1 : 2);
        int n = times.count < WB_AVATAR_MAX_KEYS ? times.count : WB_AVATAR_MAX_KEYS;
        int vcomps = values.comps;
        for (int k = 0; k < n; k++) {
            float t;
            if (read_floats(&times, k, 1, &t) != 0) break;
            oc->times[k] = t;
            read_floats(&values, k, vcomps, oc->values[k]);
        }
        oc->nkeys = n;
        if (n > 0 && oc->times[n-1] > out->duration) out->duration = oc->times[n-1];
        out->nchannels++;
    }
}

int wb_avatar_load(const char *path, wb_model *out_model, wb_rig *out_rig,
                   wb_animation *out_anims, int max_anims, int *out_nanims) {
    g_err[0] = 0;
    if (!path) return -1;

    /* meshes come from the content pipeline */
    if (out_model) {
        if (wb_asset_load_gltf(path, out_model) != 0) {
            set_err("%s", wb_asset_error() ? wb_asset_error() : "mesh load failed");
            return -1;
        }
    }

    size_t flen = 0;
    unsigned char *file = read_file(path, &flen);
    if (!file) { set_err("cannot read %s", path); return -1; }

    unsigned char *json = NULL, *bin = NULL;
    size_t json_len = 0, bin_len = 0;

    if (flen >= 12 && !memcmp(file, "glTF", 4)) {
        size_t off = 12;
        while (off + 8 <= flen) {
            unsigned clen = (unsigned)file[off] | ((unsigned)file[off+1] << 8) |
                            ((unsigned)file[off+2] << 16) | ((unsigned)file[off+3] << 24);
            unsigned ctype = (unsigned)file[off+4] | ((unsigned)file[off+5] << 8) |
                             ((unsigned)file[off+6] << 16) | ((unsigned)file[off+7] << 24);
            off += 8;
            if (off + clen > flen) break;
            if (ctype == 0x4E4F534A) { json = file + off; json_len = clen; }
            else if (ctype == 0x004E4942) { bin = file + off; bin_len = clen; }
            off += clen;
        }
    } else {
        json = file;
        json_len = flen;
    }

    if (!bin) { bin = (unsigned char *)""; bin_len = 0; }
    if (!json) { free(file); set_err("no JSON chunk in %s", path); return -1; }

    wb_json *root = wb_json_parse((const char *)json, json_len);
    if (!root) {
        free(file);
        set_err("JSON parse failed in %s", path);
        return -1;
    }

    if (out_rig) load_rig(root, bin, bin_len, out_rig);

    int nanim = 0;
    if (out_anims && max_anims > 0) {
        wb_json *anims = wb_json_get(root, "animations");
        if (anims) {
            for (int i = 0; i < anims->n && nanim < max_anims; i++)
                load_animation(root, bin, bin_len, anims->items[i], &out_anims[nanim++]);
        }
    }
    if (out_nanims) *out_nanims = nanim;

    wb_json_free(root);
    free(file);
    return 0;
}

/* ==================================================================== */
/* sampling                                                             */
/* ==================================================================== */

static void mat4_from_trs(float m[16], const float t[3], const float q[4], const float s[3]) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float x2 = x + x, y2 = y + y, z2 = z + z;
    float xx = x * x2, xy = x * y2, xz = x * z2;
    float yy = y * y2, yz = y * z2, zz = z * z2;
    float wx = w * x2, wy = w * y2, wz = w * z2;
    m[0] = (1.0f - (yy + zz)) * s[0];
    m[1] = (xy + wz) * s[0];
    m[2] = (xz - wy) * s[0];
    m[3] = 0.0f;
    m[4] = (xy - wz) * s[1];
    m[5] = (1.0f - (xx + zz)) * s[1];
    m[6] = (yz + wx) * s[1];
    m[7] = 0.0f;
    m[8] = (xz + wy) * s[2];
    m[9] = (yz - wx) * s[2];
    m[10] = (1.0f - (xx + yy)) * s[2];
    m[11] = 0.0f;
    m[12] = t[0]; m[13] = t[1]; m[14] = t[2]; m[15] = 1.0f;
}

static void quat_nlerp(const float a[4], const float b[4], float t, float out[4]) {
    float dot = a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3];
    float s = dot < 0.0f ? -1.0f : 1.0f;
    float x = a[0] + (b[0]*s - a[0]) * t;
    float y = a[1] + (b[1]*s - a[1]) * t;
    float z = a[2] + (b[2]*s - a[2]) * t;
    float w = a[3] + (b[3]*s - a[3]) * t;
    float len = sqrtf(x*x + y*y + z*z + w*w);
    if (len > 1e-9f) { x/=len; y/=len; z/=len; w/=len; }
    out[0]=x; out[1]=y; out[2]=z; out[3]=w;
}

int wb_avatar_sample(const wb_animation *a, float t,
                     float out_local[][16], int max_nodes) {
    if (!a || !out_local || max_nodes <= 0) return 0;

    /* Accumulate each node's TRS starting from its rest pose, so a channel
     * that only drives rotation leaves the authored translation alone. */
    wb_trs trs[256];
    int n = max_nodes < 256 ? max_nodes : 256;
    for (int i = 0; i < n; i++) trs[i] = a->rest[i];

    if (t < 0.0f) t = 0.0f;
    if (a->duration > 0.0f && t > a->duration) t = a->duration;

    int written = 0;
    for (int c = 0; c < a->nchannels; c++) {
        const wb_anim_channel *ch = &a->channels[c];
        if (ch->node < 0 || ch->node >= n || ch->nkeys == 0) continue;

        /* find the surrounding keyframes */
        int k = 0;
        while (k + 1 < ch->nkeys && ch->times[k+1] < t) k++;
        int k2 = (k + 1 < ch->nkeys) ? k + 1 : k;

        float alpha = 0.0f;
        if (k2 != k && ch->times[k2] > ch->times[k])
            alpha = (t - ch->times[k]) / (ch->times[k2] - ch->times[k]);

        wb_trs *r = &trs[ch->node];
        if (ch->path == 0) {
            for (int i = 0; i < 3; i++)
                r->t[i] = ch->values[k][i] + (ch->values[k2][i] - ch->values[k][i]) * alpha;
        } else if (ch->path == 1) {
            quat_nlerp(ch->values[k], ch->values[k2], alpha, r->q);
        } else {
            for (int i = 0; i < 3; i++)
                r->s[i] = ch->values[k][i] + (ch->values[k2][i] - ch->values[k][i]) * alpha;
        }
        written++;
    }

    for (int i = 0; i < n; i++)
        mat4_from_trs(out_local[i], trs[i].t, trs[i].q, trs[i].s);
    return written;
}

/* ==================================================================== */
/* identity persistence                                                  */
/* ==================================================================== */

int wb_avatar_identity_save(const char *path, const wb_avatar *a) {
    if (!path || !a) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) { set_err("cannot write %s", path); return -1; }
    fprintf(f, "{\n");
    fprintf(f, "  \"avatar_id\": \"%s\",\n", a->avatar_id);
    fprintf(f, "  \"model_path\": \"%s\",\n", a->model_path);
    fprintf(f, "  \"rig_name\": \"%s\",\n", a->rig_name);
    fprintf(f, "  \"height\": %.4f\n", a->height);
    fprintf(f, "}\n");
    fclose(f);
    return 0;
}

int wb_avatar_identity_load(const char *path, wb_avatar *a) {
    if (!path || !a) return -1;
    g_err[0] = 0;
    size_t len = 0;
    unsigned char *file = read_file(path, &len);
    if (!file) { set_err("cannot read %s", path); return -1; }
    wb_json *root = wb_json_parse((const char *)file, len);
    free(file);
    if (!root) { set_err("identity JSON parse failed"); return -1; }
    memset(a, 0, sizeof *a);
    const char *id = wb_json_str(wb_json_get(root, "avatar_id"));
    const char *mp  = wb_json_str(wb_json_get(root, "model_path"));
    const char *rn  = wb_json_str(wb_json_get(root, "rig_name"));
    if (id) snprintf(a->avatar_id, sizeof a->avatar_id, "%s", id);
    if (mp) snprintf(a->model_path, sizeof a->model_path, "%s", mp);
    if (rn) snprintf(a->rig_name, sizeof a->rig_name, "%s", rn);
    a->height = (float)wb_json_num(wb_json_get(root, "height"), 1.7);
    wb_json_free(root);
    return 0;
}

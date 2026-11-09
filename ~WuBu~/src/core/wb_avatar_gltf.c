/*
 * wb_avatar_gltf.c -- glTF skin and animation decoding. See wb_avatar_internal.h.
 */
#include "wb_avatar_internal.h"
#include "wb_gltf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void wb_avatar_load_rig(const wb_json *root, const unsigned char *bin, size_t bin_len,
                        wb_rig *rig) {
    memset(rig, 0, sizeof *rig);
    for (int i = 0; i < WB_BONE_COUNT; i++) { rig->bones[i].node = -1; rig->bones[i].parent = -1; }

    wb_json *skins = wb_json_get(root, "skins");
    if (!skins || skins->n == 0) return;
    wb_json *skin = skins->items[0];

    wb_json *joints = wb_json_get(skin, "joints");
    if (!joints) return;

    int ibm = wb_json_int(wb_json_get(skin, "inverseBindMatrices"), -1);
    wb_gltf_accessor ibm_acc;
    const unsigned char *ibm_data = NULL;
    if (ibm >= 0) ibm_data = wb_gltf_accessor_bytes(root, bin, bin_len, ibm, &ibm_acc);

    rig->njoints = joints->n < WB_AVATAR_MAX_JOINTS ? joints->n : WB_AVATAR_MAX_JOINTS;
    for (int i = 0; i < rig->njoints; i++) {
        int node = wb_json_int(joints->items[i], 0);
        rig->joint_nodes[i] = node;
        if (ibm_data && ibm_acc.comps == 16) {
            float m[16];
            if (wb_gltf_read_floats(&ibm_acc, i, 16, m) == 0)
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

void wb_avatar_load_animation(const wb_json *root, const unsigned char *bin, size_t bin_len,
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

        wb_gltf_accessor times, values;
        if (!wb_gltf_accessor_bytes(root, bin, bin_len, input_i, &times)) continue;
        if (!wb_gltf_accessor_bytes(root, bin, bin_len, output_i, &values)) continue;

        wb_anim_channel *oc = &out->channels[out->nchannels];
        memset(oc, 0, sizeof *oc);
        oc->node = node;
        oc->path = !strcmp(path, "translation") ? 0 : (!strcmp(path, "rotation") ? 1 : 2);
        int n = times.count < WB_AVATAR_MAX_KEYS ? times.count : WB_AVATAR_MAX_KEYS;
        int vcomps = values.comps;
        for (int k = 0; k < n; k++) {
            float t;
            if (wb_gltf_read_floats(&times, k, 1, &t) != 0) break;
            oc->times[k] = t;
            wb_gltf_read_floats(&values, k, vcomps, oc->values[k]);
        }
        oc->nkeys = n;
        if (n > 0 && oc->times[n - 1] > out->duration) out->duration = oc->times[n - 1];
        out->nchannels++;
    }
}

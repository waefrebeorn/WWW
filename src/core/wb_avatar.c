/*
 * wb_avatar.c -- avatar top level: container handling, load orchestration and
 * the module error state. Rig/animation decoding lives in wb_avatar_gltf.c,
 * the sampler in wb_avatar_anim.c, identity persistence in
 * wb_avatar_identity.c, the bone name table in wb_avatar_bones.c.
 */
#include "wb_avatar.h"
#include "wb_avatar_internal.h"
#include "wb_gltf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char wb_avatar_g_err[WB_AVATAR_ERR_SZ];

const char *wb_avatar_error(void) {
    return wb_avatar_g_err[0] ? wb_avatar_g_err : NULL;
}

int wb_avatar_load(const char *path, wb_model *out_model, wb_rig *out_rig,
                   wb_animation *out_anims, int max_anims, int *out_nanims) {
    wb_avatar_g_err[0] = 0;
    if (!path) return -1;

    /* meshes come from the content pipeline */
    if (out_model) {
        if (wb_asset_load_gltf(path, out_model) != 0) {
            wb_set_err(wb_avatar_g_err, sizeof wb_avatar_g_err, "%s",
                       wb_asset_error() ? wb_asset_error() : "mesh load failed");
            return -1;
        }
    }

    size_t flen = 0;
    unsigned char *file = wb_read_file(path, &flen);
    if (!file) {
        wb_set_err(wb_avatar_g_err, sizeof wb_avatar_g_err, "cannot read %s", path);
        return -1;
    }

    const char *json = NULL;
    size_t json_len = 0;
    const unsigned char *bin = NULL;
    size_t bin_len = 0;

    if (wb_gltf_is_glb(file, flen)) {
        if (wb_gltf_split_glb(file, flen, &json, &json_len, &bin, &bin_len) != 0) {
            free(file);
            wb_set_err(wb_avatar_g_err, sizeof wb_avatar_g_err,
                       "unsupported GLB in %s", path);
            return -1;
        }
    } else {
        json = (const char *)file;
        json_len = flen;
    }

    if (!bin) { bin = (const unsigned char *)""; bin_len = 0; }
    if (!json) {
        free(file);
        wb_set_err(wb_avatar_g_err, sizeof wb_avatar_g_err, "no JSON chunk in %s", path);
        return -1;
    }

    wb_json *root = wb_json_parse(json, json_len);
    if (!root) {
        free(file);
        wb_set_err(wb_avatar_g_err, sizeof wb_avatar_g_err, "JSON parse failed in %s", path);
        return -1;
    }

    if (out_rig) wb_avatar_load_rig(root, bin, bin_len, out_rig);

    int nanim = 0;
    if (out_anims && max_anims > 0) {
        wb_json *anims = wb_json_get(root, "animations");
        if (anims) {
            for (int i = 0; i < anims->n && nanim < max_anims; i++)
                wb_avatar_load_animation(root, bin, bin_len, anims->items[i],
                                         &out_anims[nanim++]);
        }
    }
    if (out_nanims) *out_nanims = nanim;

    wb_json_free(root);
    free(file);
    return 0;
}

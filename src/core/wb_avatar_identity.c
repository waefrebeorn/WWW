/*
 * wb_avatar_identity.c -- identity record persistence (JSON). See wb_avatar.h.
 */
#include "wb_avatar.h"
#include "wb_avatar_internal.h"
#include "wb_json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int wb_avatar_identity_save(const char *path, const wb_avatar *a) {
    if (!path || !a) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) {
        wb_set_err(wb_avatar_g_err, sizeof wb_avatar_g_err, "cannot write %s", path);
        return -1;
    }
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
    wb_avatar_g_err[0] = 0;
    size_t len = 0;
    unsigned char *file = wb_read_file(path, &len);
    if (!file) {
        wb_set_err(wb_avatar_g_err, sizeof wb_avatar_g_err, "cannot read %s", path);
        return -1;
    }
    wb_json *root = wb_json_parse((const char *)file, len);
    free(file);
    if (!root) {
        wb_set_err(wb_avatar_g_err, sizeof wb_avatar_g_err, "identity JSON parse failed");
        return -1;
    }
    memset(a, 0, sizeof *a);
    const char *id = wb_json_str(wb_json_get(root, "avatar_id"));
    const char *mp = wb_json_str(wb_json_get(root, "model_path"));
    const char *rn = wb_json_str(wb_json_get(root, "rig_name"));
    if (id) snprintf(a->avatar_id, sizeof a->avatar_id, "%s", id);
    if (mp) snprintf(a->model_path, sizeof a->model_path, "%s", mp);
    if (rn) snprintf(a->rig_name, sizeof a->rig_name, "%s", rn);
    a->height = (float)wb_json_num(wb_json_get(root, "height"), 1.7);
    wb_json_free(root);
    return 0;
}

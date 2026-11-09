/*
 * wb_assets_manifest.c -- asset manifest load/write/verify. See wb_manifest.h.
 */
#include "wb_manifest.h"
#include "wb_assets_internal.h"
#include "wb_json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void wb_manifest_free(wb_manifest *mf) {
    if (!mf) return;
    for (int i = 0; i < mf->count; i++) free(mf->entries[i].path);
    free(mf->entries);
    mf->entries = NULL;
    mf->count = 0;
}

int wb_manifest_load(const char *path, wb_manifest *out) {
    wb_assets_g_err[0] = 0;
    if (!path || !out) return -1;
    memset(out, 0, sizeof *out);
    size_t len = 0;
    unsigned char *file = wb_read_file(path, &len);
    if (!file) {
        wb_set_err(wb_assets_g_err, sizeof wb_assets_g_err,
                   "cannot read manifest %s", path);
        return -1;
    }
    wb_json *root = wb_json_parse((const char *)file, len);
    free(file);
    if (!root) {
        wb_set_err(wb_assets_g_err, sizeof wb_assets_g_err, "manifest JSON parse failed");
        return -1;
    }
    wb_json *assets = wb_json_get(root, "assets");
    if (!assets || assets->type != WB_JSON_ARR) {
        wb_json_free(root);
        wb_set_err(wb_assets_g_err, sizeof wb_assets_g_err, "manifest has no assets array");
        return -1;
    }
    out->entries = (wb_manifest_entry *)calloc((size_t)assets->n, sizeof(wb_manifest_entry));
    if (!out->entries) { wb_json_free(root); return -1; }
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
    if (!f) {
        wb_set_err(wb_assets_g_err, sizeof wb_assets_g_err,
                   "cannot write manifest %s", path);
        return -1;
    }
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

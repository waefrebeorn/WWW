/*
 * wb_mod.c -- WuBu Engine modding sandbox (slice 15). See wb_mod.h.
 */
#include "wb_mod.h"
#include "wb_script.h"
#include "wb_space_bind.h"
#include "wb_worldspace.h"
#include "wb_json.h"
#include "wb_util.h"

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct wb_mod {
    char dir[800];
    wb_mod_manifest manifest;
    wb_world *w;
    wb_worldspace *ws;          /* the space this mod's gametype belongs to (may be NULL) */
    char self_mode[WB_MODE_NAME_LEN];
    lua_State *L;
    char source_hash[17];
    char err[1024];
    int step_ran;
};

static char g_manifest_err[256];
const char *wb_mod_manifest_error(void) { return g_manifest_err[0] ? g_manifest_err : NULL; }

/* ------------------------------------------------------------- manifest */

int wb_mod_manifest_load(const char *path, wb_mod_manifest *out) {
    g_manifest_err[0] = 0;
    memset(out, 0, sizeof *out);
    size_t len = 0;
    unsigned char *file = wb_read_file(path, &len);
    if (!file) { snprintf(g_manifest_err, sizeof g_manifest_err, "cannot read %s", path); return -1; }
    wb_json *root = wb_json_parse((const char *)file, len);
    free(file);
    if (!root) { snprintf(g_manifest_err, sizeof g_manifest_err, "manifest JSON parse failed"); return -1; }

    const char *id   = wb_json_str(wb_json_get(root, "id"));
    const char *name = wb_json_str(wb_json_get(root, "name"));
    const char *ver  = wb_json_str(wb_json_get(root, "version"));
    const char *ent  = wb_json_str(wb_json_get(root, "entry"));
    const char *auth = wb_json_str(wb_json_get(root, "author"));
    if (id)   snprintf(out->id,   sizeof out->id,   "%s", id);
    if (name) snprintf(out->name, sizeof out->name, "%s", name);
    if (ver)  snprintf(out->version, sizeof out->version, "%s", ver);
    if (ent)  snprintf(out->entry, sizeof out->entry, "%s", ent);
    if (auth) snprintf(out->author, sizeof out->author, "%s", auth);
    out->budget     = (long)wb_json_num(wb_json_get(root, "budget"), 100000.0);
    out->hot_reload = wb_json_int(wb_json_get(root, "hot_reload"), 1);

    wb_json_free(root);
    return 0;
}

int wb_mod_manifest_validate(const wb_mod_manifest *m) {
    if (!m) return -1;
    if (!m->id[0])      { snprintf(g_manifest_err, sizeof g_manifest_err, "manifest has no id"); return -1; }
    if (!m->entry[0])   { snprintf(g_manifest_err, sizeof g_manifest_err, "manifest has no entry script"); return -1; }
    if (!m->version[0]) { snprintf(g_manifest_err, sizeof g_manifest_err, "manifest has no version"); return -1; }
    return 0;
}

/* ----------------------------------------------------------------- mod */

const wb_mod_manifest *wb_mod_get_manifest(const wb_mod *mod) { return &mod->manifest; }
const char *wb_mod_error(const wb_mod *mod) { return mod->err[0] ? mod->err : NULL; }
int wb_mod_budget_exceeded(const wb_mod *mod) {
    return mod->L ? wb_script_budget_exceeded(mod->L) : 0;
}

wb_mod *wb_mod_load(const char *dir, const wb_mod_manifest *m, wb_world *w) {
    return wb_mod_load_space(dir, m, w, NULL, NULL);
}

/* Load a mod and attach it to the space (slice 21). `ws` is the worldspace the
 * mod's gametype lives in; `self_mode` names that gametype, so the mod can
 * declare gates only in its own world. Both may be NULL (no space bindings). */
wb_mod *wb_mod_load_space(const char *dir, const wb_mod_manifest *m, wb_world *w,
                          wb_worldspace *ws, const char *self_mode) {
    if (!m) return NULL;
    wb_mod *mod = (wb_mod *)calloc(1, sizeof *mod);
    if (!mod) return NULL;
    snprintf(mod->dir, sizeof mod->dir, "%s", dir ? dir : "");
    mod->manifest = *m;
    mod->w = w;
    mod->ws = ws;
    snprintf(mod->self_mode, sizeof mod->self_mode, "%s", self_mode ? self_mode : "");

    char path[800];
    wb_join_path(path, sizeof path, dir, m->entry);
    size_t len = 0;
    unsigned char *src = wb_read_file(path, &len);
    if (!src) {
        snprintf(mod->err, sizeof mod->err, "cannot read entry script %s", path);
        free(mod);
        return NULL;
    }

    mod->L = wb_script_create_sandbox_state(w, m->budget);
    if (!mod->L) {
        snprintf(mod->err, sizeof mod->err, "lua state creation failed");
        free(src); free(mod);
        return NULL;
    }
    if (wb_script_sandbox_attach_space(mod->L, ws, mod->self_mode) != 0) {
        snprintf(mod->err, sizeof mod->err, "cannot attach the space");
        free(src); wb_mod_free(mod);
        return NULL;
    }
    if (wb_space_bind_install(mod->L) != 0) {
        snprintf(mod->err, sizeof mod->err, "cannot install the space bindings");
        free(src); wb_mod_free(mod);
        return NULL;
    }
    if (wb_script_run_sandbox(mod->L, (const char *)src, len, m->entry) != 0) {
        snprintf(mod->err, sizeof mod->err, "entry script failed to run");
        free(src); wb_mod_free(mod);
        return NULL;
    }
    free(src);

    if (wb_content_hash(path, mod->source_hash) != 0) mod->source_hash[0] = 0;
    return mod;
}

void wb_mod_free(wb_mod *mod) {
    if (!mod) return;
    if (mod->L) lua_close(mod->L);
    free(mod);
}

int wb_mod_step(wb_mod *mod, double dt) {
    if (!mod || !mod->L) return -1;
    int rc = wb_script_step(mod->L, (float)dt);
    mod->step_ran = (rc == 1);
    if (rc < 0) {
        snprintf(mod->err, sizeof mod->err, "step failed (instruction budget exceeded: %s)",
                 wb_mod_budget_exceeded(mod) ? "yes" : "no");
    }
    return rc;
}

int wb_mod_reload(wb_mod *mod) {
    if (!mod) return -1;
    char path[800];
    wb_join_path(path, sizeof path, mod->dir, mod->manifest.entry);
    size_t len = 0;
    unsigned char *src = wb_read_file(path, &len);
    if (!src) { snprintf(mod->err, sizeof mod->err, "cannot re-read %s", path); return -1; }

    /* fresh state: no stale globals from the previous version survive */
    if (mod->L) lua_close(mod->L);
    mod->L = wb_script_create_sandbox_state(mod->w, mod->manifest.budget);
    mod->err[0] = 0;
    if (!mod->L) { free(src); snprintf(mod->err, sizeof mod->err, "lua state creation failed"); return -1; }
    if (wb_script_sandbox_attach_space(mod->L, mod->ws, mod->self_mode) != 0) {
        free(src);
        snprintf(mod->err, sizeof mod->err, "cannot attach the space");
        return -1;
    }
    if (wb_space_bind_install(mod->L) != 0) {
        free(src);
        snprintf(mod->err, sizeof mod->err, "cannot install the space bindings");
        return -1;
    }

    if (wb_script_run_sandbox(mod->L, (const char *)src, len, mod->manifest.entry) != 0) {
        snprintf(mod->err, sizeof mod->err, "reloaded script failed to run");
        free(src);
        return -1;
    }
    free(src);
    wb_content_hash(path, mod->source_hash);
    return 0;
}

int wb_mod_get_global_number(wb_mod *mod, const char *name, double *out) {
    if (!mod || !mod->L || !name || !out) return -1;
    return wb_script_get_sandbox_number(mod->L, name, out);
}

int wb_mod_needs_reload(const wb_mod *mod) {
    if (!mod || !mod->manifest.hot_reload || !mod->source_hash[0]) return 0;
    char path[800];
    wb_join_path(path, sizeof path, mod->dir, mod->manifest.entry);
    char now[17];
    if (wb_content_hash(path, now) != 0) return 0;
    return strcmp(now, mod->source_hash) != 0;
}

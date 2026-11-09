/*
 * wb_mod.h -- WuBu Engine modding sandbox (slice 15).
 *
 * Untrusted creator code, run the way Luau runs it in Roblox: each mod gets a
 * fresh Lua state, a whitelisted global environment, a capability table instead
 * of raw OS access, and a hard per-step instruction budget so a runaway script
 * cannot hang the host. A mod is described by a manifest and can be hot
 * reloaded without restarting the world.
 *
 * C11. The Lua host is the vendored Lua 5.4.
 */
#ifndef WB_MOD_H
#define WB_MOD_H

#include "wb_ecs.h"

#ifdef __cplusplus
extern "C" {
#endif

struct wb_worldspace;

typedef struct {
    char id[64];
    char name[128];
    char version[32];
    char entry[256];
    char author[64];
    long budget;              /* Lua instruction budget per step; <=0 = no cap */
    int  hot_reload;          /* reload when the entry file changes */
} wb_mod_manifest;

/* Load a mod manifest (JSON). Returns 0 on success. */
int wb_mod_manifest_load(const char *path, wb_mod_manifest *out);
/* Sanity-check a manifest. Returns 0 when it is usable. */
int wb_mod_manifest_validate(const wb_mod_manifest *m);
const char *wb_mod_manifest_error(void);

typedef struct wb_mod wb_mod;

/* Load a mod from `dir` (the manifest's directory). The mod's entry script is
 * compiled and run immediately. Returns NULL on failure. */
wb_mod *wb_mod_load(const char *dir, const wb_mod_manifest *m, wb_world *w);

/* Load a mod and attach it to a worldspace (slice 21). `self_mode` names the
 * gametype the mod belongs to, so its wb.add_gate calls land in its own world.
 * `ws`/`self_mode` may be NULL: the mod then has no space bindings, exactly
 * like wb_mod_load. */
wb_mod *wb_mod_load_space(const char *dir, const wb_mod_manifest *m, wb_world *w,
                          struct wb_worldspace *ws, const char *self_mode);
void    wb_mod_free(wb_mod *mod);

/* Run one mod step (calls the script's global step(dt)).
 * Returns 1 = ran, 0 = no step function, -1 = error. */
int  wb_mod_step(wb_mod *mod, double dt);

/* Hot reload: re-read the entry script into a FRESH state (so no stale
 * globals survive) and run it. Returns 0 on success. */
int  wb_mod_reload(wb_mod *mod);

/* True when the entry file's content hash has changed since load. */
int  wb_mod_needs_reload(const wb_mod *mod);

int  wb_mod_budget_exceeded(const wb_mod *mod);
const char *wb_mod_error(const wb_mod *mod);
const wb_mod_manifest *wb_mod_get_manifest(const wb_mod *mod);

/* Read a numeric global from the mod's sandbox (for tests and tooling). */
int wb_mod_get_global_number(wb_mod *mod, const char *name, double *out);

#ifdef __cplusplus
}
#endif

#endif /* WB_MOD_H */

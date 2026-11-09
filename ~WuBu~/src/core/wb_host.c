/*
 * wb_host.c -- the gametype host (slice 22). See wb_host.h.
 */
#include "wb_host.h"
#include "wb_script.h"
#include "wb_space_bind.h"
#include "wb_worldspace.h"
#include "wb_util.h"

#include <lua.h>
#include <lauxlib.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct wb_host {
    wb_worldspace *ws;
    wb_world      *world;
    lua_State     *L;
    wb_entity      player;      /* the player's body in the mode's world */
    char mode[WB_MODE_NAME_LEN];
    char instance[WB_DIST_ID_LEN];
    char err[1024];
};

const char *wb_host_error(const wb_host *h) {
    return (h && h->err[0]) ? h->err : NULL;
}
const char *wb_host_mode(const wb_host *h)   { return h ? h->mode : NULL; }
const char *wb_host_instance(const wb_host *h) { return h ? h->instance : NULL; }
wb_world *wb_host_world(wb_host *h)         { return h ? h->world : NULL; }
wb_worldspace *wb_host_space(wb_host *h)    { return h ? h->ws : NULL; }
wb_entity wb_host_player(const wb_host *h)  { return h ? h->player : (wb_entity){0, 0}; }

/* Register every immediate subdirectory of `root` that holds a mode.json.
 * Returns the number registered, or -1 on the first failure. */
static int register_modes_in(wb_worldspace *ws, const char *root,
                             char *err, size_t err_sz) {
    DIR *d = opendir(root);
    if (!d) {
        snprintf(err, err_sz, "cannot open gametypes directory %s", root);
        return -1;
    }
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;          /* . .. and dotfiles */
        char dir[800];
        wb_join_path(dir, sizeof dir, root, e->d_name);
        char manifest[900];
        wb_join_path(manifest, sizeof manifest, dir, "mode.json");
        if (!wb_file_exists(manifest)) continue;    /* not a gametype */

        if (wb_worldspace_register_mode(ws, dir, err, err_sz) != 0) {
            closedir(d);
            return -1;
        }
        /* Install the mode's DECLARED spawn points. These must exist before any
         * script runs, or a mode could not be travelled into. */
        const wb_mode_manifest *m = wb_worldspace_mode_at(ws, n);
        for (int i = 0; m && i < m->nspawns; i++) {
            wb_worldspace_add_spawn(ws, m->name, m->spawns[i].pos,
                                    m->spawns[i].team, m->spawns[i].tag);
        }
        n++;
    }
    closedir(d);
    return n;
}

wb_host *wb_host_open(const char *modes_root, const char *gamemode,
                      const char *player_id, int team,
                      char *err, size_t err_sz) {
    if (!modes_root || !gamemode || !gamemode[0]) {
        if (err) snprintf(err, err_sz, "a modes directory and a gametype are required");
        return NULL;
    }

    wb_host *h = (wb_host *)calloc(1, sizeof *h);
    if (!h) {
        if (err) snprintf(err, err_sz, "out of memory");
        return NULL;
    }

    h->ws = wb_worldspace_open();
    if (!h->ws) {
        snprintf(h->err, sizeof h->err, "cannot open the world space");
        goto fail;
    }

    int n = register_modes_in(h->ws, modes_root, h->err, sizeof h->err);
    if (n < 0) goto fail;
    if (n == 0) {
        snprintf(h->err, sizeof h->err,
                 "no gametypes under %s (each needs a mode.json)", modes_root);
        goto fail;
    }
    if (!wb_worldspace_mode(h->ws, gamemode)) {
        snprintf(h->err, sizeof h->err,
                 "gametype '%s' is not one of the %d registered under %s",
                 gamemode, n, modes_root);
        goto fail;
    }

    /* The world this mode runs in. */
    h->world = wb_world_new();
    if (!h->world) {
        snprintf(h->err, sizeof h->err, "out of memory");
        goto fail;
    }
    wb_world_setup_comps(h->world);

    /* The mode's script, bound to its world and carrying its space context --
     * so wb.add_spawn / wb.add_gate land in the mode's OWN world. */
    h->L = wb_script_create_state(h->world, h->ws, gamemode);
    if (!h->L) {
        snprintf(h->err, sizeof h->err, "cannot create the mode's Lua state");
        goto fail;
    }
    if (wb_space_bind_install(h->L) != 0) {
        snprintf(h->err, sizeof h->err, "cannot install the space bindings");
        goto fail;
    }

    /* Run the entry script FIRST: it declares the mode's spawns and stadiums,
     * and only then can a player be put into the world. */
    char mode_dir[600], entry[800];
    if (wb_worldspace_mode_dir(h->ws, gamemode, mode_dir, sizeof mode_dir) != 0 ||
        wb_mode_entry_path(wb_worldspace_mode(h->ws, gamemode), mode_dir,
                           entry, sizeof entry) != 0) {
        snprintf(h->err, sizeof h->err, "cannot resolve %s's entry script", gamemode);
        goto fail;
    }
    if (luaL_dofile(h->L, entry) != LUA_OK) {
        snprintf(h->err, sizeof h->err, "%s: %s", gamemode, lua_tostring(h->L, -1));
        lua_pop(h->L, 1);
        goto fail;
    }

    /* NOW spawn the player, at a point the mode itself declared. */
    float pos[3];
    if (wb_worldspace_spawn(h->ws, gamemode, NULL, player_id, team,
                            h->instance, pos, h->err, sizeof h->err) != 0) {
        goto fail;
    }

    /* Materialize them as a body in the mode's world -- the space picked WHERE,
     * this puts an entity THERE -- and hand the handle to the script as
     * wb.player, so the mode can see and move them. */
    h->player = wb_world_spawn_entity(h->world, player_id, pos);
    lua_getglobal(h->L, "wb");
    if (lua_istable(h->L, -1)) {
        wb_script_push_entity(h->L, h->player);
        lua_setfield(h->L, -2, "player");
        lua_pushstring(h->L, player_id);
        lua_setfield(h->L, -2, "player_id");
        lua_pop(h->L, 1);
    } else {
        lua_pop(h->L, 1);
    }

    snprintf(h->mode, sizeof h->mode, "%s", gamemode);
    h->err[0] = 0;
    return h;

fail:
    if (err) snprintf(err, err_sz, "%s", h->err);
    wb_host_close(h);
    return NULL;
}

void wb_host_close(wb_host *h) {
    if (!h) return;
    if (h->L) lua_close(h->L);
    if (h->world) wb_world_destroy(h->world);
    if (h->ws) wb_worldspace_close(h->ws);
    free(h);
}

int wb_host_step(wb_host *h, float dt) {
    if (!h || !h->L) return -1;
    return wb_script_step(h->L, dt);
}

int wb_host_running(wb_host *h) {
    if (!h || !h->L) return 0;
    lua_getglobal(h->L, "wb");
    if (!lua_istable(h->L, -1)) { lua_pop(h->L, 1); return 0; }
    lua_getfield(h->L, -1, "running");
    int running = lua_isnil(h->L, -1) ? 1 : lua_toboolean(h->L, -1);
    lua_pop(h->L, 2);
    return running;
}

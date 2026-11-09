/*
 * wb_exp.c -- experience container (slice 7).
 *
 * One instantiation of a gamemode spawned into a tile-cell. Owns a Lua state
 * (via the shared script host) bound to its own ECS world, so two instances
 * of the same gamemode share neider Lua globals nor entities. "spawn into a
 * game space" = wb_exp_join adds a player entity to the experience's world.
 */
#include "wb_exp.h"
#include "wb_ecs.h"
#include "wb_script.h"
#include <lua.h>
#include <lauxlib.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* components a gamemode expects; same schema as the script host */
enum { CPOS, CVEL, CMASS, CLABEL, kComps };

struct wb_exp {
    char         name[64];
    wb_qkey      cell;
    wb_world    *world;
    lua_State   *L;
    uint32_t     tick;
};

wb_exp *wb_exp_spawn(const wb_exp_cfg *cfg, const char *gamemode_name) {
    if (!cfg || !cfg->lua_path) return NULL;

    wb_exp *e = calloc(1, sizeof(*e));
    if (!e) return NULL;

    snprintf(e->name, sizeof(e->name), "%s", gamemode_name ? gamemode_name : "game");
    e->cell = cfg->cell;
    e->tick = 0;

    e->world = wb_world_new();
    if (!e->world) { free(e); return NULL; }
    wb_world_setup_comps(e->world);

    e->L = wb_script_create_state(e->world, NULL, NULL);
    if (!e->L) { wb_world_destroy(e->world); free(e); return NULL; }

    if (luaL_dofile(e->L, cfg->lua_path) == LUA_OK) {
        return e;   /* gamemode script loaded into this instance */
    }
    /* load failed: teardown and report */
    fprintf(stderr, "wb_exp: gamemode '%s' failed to load: %s\n",
            e->name, lua_tostring(e->L, -1));
    lua_pop(e->L, 1);
    wb_exp_dispose(e);
    return NULL;
}

void wb_exp_dispose(wb_exp *e) {
    if (!e) return;
    if (e->L) lua_close(e->L);
    if (e->world) wb_world_destroy(e->world);
    free(e);
}

const char *wb_exp_name(const wb_exp *e) { return e ? e->name : NULL; }
wb_qkey     wb_exp_cell(const wb_exp *e)  { return e ? e->cell : 0; }
void       *wb_exp_world(wb_exp *e)       { return e ? (void *)e->world : NULL; }

int wb_exp_join(wb_exp *e, const char *player_id) {
    if (!e) return 0;
    wb_entity ent = wb_entity_new(e->world);
    float *p = (float *)wb_comp_add(e->world, ent, CPOS);
    float *v = (float *)wb_comp_add(e->world, ent, CVEL);
    float *m = (float *)wb_comp_add(e->world, ent, CMASS);
    char  *lbl = (char *)wb_comp_add(e->world, ent, CLABEL);
    p[0]=0.0f; p[1]=0.0f; p[2]=0.0f;
    v[0]=0.0f; v[1]=0.0f; v[2]=0.0f;
    *m=1.0f;
    snprintf(lbl, 32, "%s", player_id ? player_id : "player");
    return 1;
}

uint32_t wb_exp_step(wb_exp *e, float dt) {
    if (!e) return 0;
    /* gamemode update first (Lua mutates velocity), then C integrates */
    wb_script_step(e->L, dt);
    wb_physics_tick(e->world, dt);
    e->tick++;
    return wb_entity_count(e->world);
}

/*
 * wb_lua_host.c -- Embedded Lua 5.4 host for WuBu Engine.
 *
 * The C#-replacement contract: C11 owns the ECS core + a thin physics tick;
 * Lua owns game logic. Lua scripts spawn entities, tag them with components,
 * and drive a per-tick update. This file binds the core to Lua.
 *
 * Build: gcc -std=c11 -Ithirdparty/lua5.4 -Isrc/core wb_lua_host.c \
 *        wb_ecs.c thirdparty/lua5.4/*.c -o wubu_engine -lm
 */
#include "wb_ecs.h"
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <stdio.h>
#include <string.h>

/* components registered at init; Lua sees them by name */
static const struct { const char *name; size_t size; } kComps[] = {
    {"position", sizeof(float) * 3}, /* x,y,z */
    {"velocity", sizeof(float) * 3}, /* vx,vy,vz */
    {"mass",     sizeof(float)},      /* single float */
    {"label",    sizeof(char[32])},   /* name tag */
};
#define POS      0
#define VEL      1
#define MASS     2
#define LABEL    3
#define kCompCount (sizeof(kComps)/sizeof(kComps[0]))

/* world pointer handed through the Lua registry */
#define WB_META_WORLD  "wb_world_ptr"

static void push_v3(lua_State *L, const float *v) {
    lua_createtable(L, 3, 0);
    for (int i = 0; i < 3; i++) { lua_pushnumber(L, v[i]); lua_rawseti(L, -2, i + 1); }
}
static void read_v3(lua_State *L, int idx, float *out) {
    for (int i = 1; i <= 3; i++) {
        lua_rawgeti(L, idx, i);
        out[i - 1] = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
    }
}

static wb_world *get_world(lua_State *L) {
    lua_getfield(L, LUA_REGISTRYINDEX, WB_META_WORLD);
    wb_world *w = (wb_world *)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return w;
}

static void err(const char *msg) { fprintf(stderr, "wb: %s\n", msg); }

/* lua: wb.binding.spawn(label) -> entity handle (a lightuserdata) */
static int l_spawn(lua_State *L) {
    wb_world *w = get_world(L);
    const char *label = lua_tostring(L, 1);
    wb_entity e = wb_entity_new(w);
    /* spawn with position/velocity/mass/label all present */
    float *p = wb_comp_add(w, e, POS);
    float *v = wb_comp_add(w, e, VEL);
    float *m = wb_comp_add(w, e, MASS);
    char *lbl = wb_comp_add(w, e, LABEL);
    p[0]=p[1]=p[2]=0.0f; v[0]=v[1]=v[2]=0.0f; *m=1.0f;
    snprintf(lbl, 32, "%s", label ? label : "anon");
    /* encode handle as two integers in a table */
    lua_createtable(L, 0, 2);
    lua_pushinteger(L, e.index); lua_setfield(L, -2, "i");
    lua_pushinteger(L, e.gen);   lua_setfield(L, -2, "g");
    return 1;
}

static wb_entity read_entity(lua_State *L, int idx) {
    wb_entity e = {0,0};
    lua_getfield(L, idx, "i"); e.index = (uint32_t)lua_tointeger(L,-1); lua_pop(L,1);
    lua_getfield(L, idx, "g"); e.gen   = (uint32_t)lua_tointeger(L,-1); lua_pop(L,1);
    return e;
}

/* lua: wb.binding.set_pos(e, {x,y,z}) */
static int l_set_pos(lua_State *L) {
    wb_world *w = get_world(L);
    wb_entity e = read_entity(L, 1);
    float *p = wb_comp_get(w, e, POS);
    if (!p) { err("set_pos: entity missing position"); return 0; }
    read_v3(L, 2, p);
    return 0;
}
/* lua: wb.binding.set_vel(e, {x,y,z}) */
static int l_set_vel(lua_State *L) {
    wb_world *w = get_world(L);
    wb_entity e = read_entity(L, 1);
    float *v = wb_comp_get(w, e, VEL);
    if (!v) { err("set_vel: entity missing velocity"); return 0; }
    read_v3(L, 2, v);
    return 0;
}
/* lua: wb.binding.get_pos(e) -> {x,y,z} */
static int l_get_pos(lua_State *L) {
    wb_world *w = get_world(L);
    wb_entity e = read_entity(L, 1);
    float *p = wb_comp_get(w, e, POS);
    if (!p) { lua_pushnil(L); return 1; }
    push_v3(L, p);
    return 1;
}
/* lua: wb.binding.count() -> n */
static int l_count(lua_State *L) {
    wb_world *w = get_world(L);
    lua_pushinteger(L, wb_entity_count(w));
    return 1;
}
/* lua: wb.binding.set_mass(e, m) */
static int l_set_mass(lua_State *L) {
    wb_world *w = get_world(L);
    wb_entity e = read_entity(L, 1);
    float *m = wb_comp_get(w, e, MASS);
    if (m) *m = (float)lua_tonumber(L, 2);
    return 0;
}

static const luaL_Reg kBindings[] = {
    {"spawn",   l_spawn},
    {"set_pos", l_set_pos},
    {"set_vel", l_set_vel},
    {"get_pos", l_get_pos},
    {"set_mass",l_set_mass},
    {"count",   l_count},
    {NULL, NULL},
};

/* C-side physics tick: velocity integrates position with a fixed dt + drag. */
void wb_physics_tick(wb_world *w, float dt) {
    float drag = 0.995f;
    for (uint32_t i = 0; i < w->next_index; i++) {
        if (!w->alive[i]) continue;
        float *p = (float *)(w->cols[POS].data + (size_t)i * w->cols[POS].elem_size);
        float *v = (float *)(w->cols[VEL].data + (size_t)i * w->cols[VEL].elem_size);
        if (!p || !v) continue;
        uint8_t *pre = w->cols[POS].present;
        if (!(pre[i >> 3] >> (i & 7) & 1)) continue;
        uint8_t *vpre = w->cols[VEL].present;
        if (!(vpre[i >> 3] >> (i & 7) & 1)) continue;
        for (int c = 0; c < 3; c++) {
            v[c] *= drag;
            p[c] += v[c] * dt;
        }
    }
}

int main(int argc, char **argv) {
    const char *script = argc > 1 ? argv[1] : "game/main.lua";

    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    wb_world world;
    wb_world_init(&world);
    for (uint32_t i = 0; i < kCompCount; i++)
        wb_register_comp(&world, kComps[i].name, kComps[i].size);

    /* stash the world in the registry so bindings can reach it */
    lua_pushlightuserdata(L, &world);
    lua_setfield(L, LUA_REGISTRYINDEX, WB_META_WORLD);

    luaL_newlib(L, kBindings);
    lua_setglobal(L, "wb");

    if (luaL_dofile(L, script) != LUA_OK) {
        err(lua_tostring(L, -1));
        lua_pop(L, 1);
        wb_world_free(&world);
        lua_close(L);
        return 1;
    }

    /* main loop: frame = dofix(update) until %input.stop == true */
    int guard = 0;
    while (guard++ < 120000) {
        lua_getglobal(L, "wb");
        lua_getfield(L, -1, "running");
        if (lua_isnil(L, -1)) break;
        int running = lua_toboolean(L, -1);
        lua_pop(L, 1);
        if (!running) break;
        lua_pop(L, 1);

        lua_getglobal(L, "step");
        if (lua_isnil(L, -1)) { lua_pop(L, 1); break; }
        lua_pushnumber(L, 1.0f / 60.0f);
        if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
            err(lua_tostring(L, -1));
            lua_pop(L, 1);
            break;
        }
        /* pcall with 0 results already left the stack back to empty */

        /* C-side ownership: the host integrates physics every frame. Lua
         * mutated velocities via step(); the core advances positions. */
        wb_physics_tick(&world, 1.0f / 60.0f);
    }

    wb_world_free(&world);
    lua_close(L);
    printf("wb: engine exited clean after %d ticks\n", guard - 1);
    return 0;
}

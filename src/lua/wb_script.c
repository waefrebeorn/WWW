/*
 * wb_script.c -- shared Lua<->core binding (slice 7).
 *
 * The functions that both the standalone engine and experience containers
 * use: build a Lua state bound to a world, register the wb.* bindings, run a
 * gamemode step, and the C-side physics integration. No main() here -- this is
 * the library half; wb_lua_host.c is the standalone entrypoint.
 */
#include "wb_ecs.h"
#include "wb_script.h"
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <stdio.h>
#include <string.h>

/* components registered at init; Lua sees them by name */
static const struct { const char *name; size_t size; } kComps[] = {
    {"position", sizeof(float) * 3},
    {"velocity", sizeof(float) * 3},
    {"mass",     sizeof(float)},
    {"label",    sizeof(char[32])},
};
#define POS 0
#define VEL 1
#define MASS 2
#define LABEL 3
#define kCompCount (sizeof(kComps)/sizeof(kComps[0]))

#define WB_META_WORLD "wb_world_ptr"

/* register the component schema on a world (idempotent per new world) */
void wb_world_setup_comps(wb_world *w) {
    for (uint32_t i = 0; i < kCompCount; i++)
        wb_register_comp(w, kComps[i].name, kComps[i].size);
}

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

static int l_spawn(lua_State *L) {
    wb_world *w = get_world(L);
    const char *label = lua_tostring(L, 1);
    wb_entity e = wb_entity_new(w);
    float *p = wb_comp_add(w, e, POS);
    float *v = wb_comp_add(w, e, VEL);
    float *m = wb_comp_add(w, e, MASS);
    char *lbl = wb_comp_add(w, e, LABEL);
    p[0]=p[1]=p[2]=0.0f; v[0]=v[1]=v[2]=0.0f; *m=1.0f;
    snprintf(lbl, 32, "%s", label ? label : "anon");
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
static int l_set_pos(lua_State *L) {
    wb_world *w = get_world(L);
    float *p = wb_comp_get(w, read_entity(L, 1), POS);
    if (!p) { err("set_pos: entity missing position"); return 0; }
    read_v3(L, 2, p);
    return 0;
}
static int l_set_vel(lua_State *L) {
    wb_world *w = get_world(L);
    float *v = wb_comp_get(w, read_entity(L, 1), VEL);
    if (!v) { err("set_vel: entity missing velocity"); return 0; }
    read_v3(L, 2, v);
    return 0;
}
static int l_get_pos(lua_State *L) {
    wb_world *w = get_world(L);
    float *p = wb_comp_get(w, read_entity(L, 1), POS);
    if (!p) { lua_pushnil(L); return 1; }
    push_v3(L, p);
    return 1;
}
static int l_count(lua_State *L) {
    wb_world *w = get_world(L);
    lua_pushinteger(L, wb_entity_count(w));
    return 1;
}
static int l_set_mass(lua_State *L) {
    wb_world *w = get_world(L);
    float *m = wb_comp_get(w, read_entity(L, 1), MASS);
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

lua_State *wb_script_create_state(wb_world *w) {
    lua_State *L = luaL_newstate();
    if (!L) return NULL;
    luaL_openlibs(L);
    lua_pushlightuserdata(L, w);
    lua_setfield(L, LUA_REGISTRYINDEX, WB_META_WORLD);
    luaL_newlib(L, kBindings);
    lua_setglobal(L, "wb");
    return L;
}

int wb_script_step(lua_State *L, float dt) {
    lua_getglobal(L, "step");
    if (lua_isnil(L, -1)) { lua_pop(L, 1); return 0; }
    lua_pushnumber(L, dt);
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        fprintf(stderr, "wb: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return -1;
    }
    return 1;
}

void wb_physics_tick(wb_world *w, float dt) {
    float drag = 0.995f;
    for (uint32_t i = 0; i < w->next_index; i++) {
        if (!w->alive[i]) continue;
        uint8_t *vpre = w->cols[VEL].present;
        uint8_t *pre = w->cols[POS].present;
        if (!(pre[i >> 3] >> (i & 7) & 1)) continue;
        if (!(vpre[i >> 3] >> (i & 7) & 1)) continue;
        float *p = (float *)(w->cols[POS].data + (size_t)i * w->cols[POS].elem_size);
        float *v = (float *)(w->cols[VEL].data + (size_t)i * w->cols[VEL].elem_size);
        for (int c = 0; c < 3; c++) {
            v[c] *= drag;
            p[c] += v[c] * dt;
        }
    }
}

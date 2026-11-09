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
#include "wb_mode.h"
#include "wb_worldspace.h"
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* components registered at init; Lua sees them by name */
static const struct { const char *name; size_t size; } kComps[] = {
    {"position", sizeof(float) * 3},
    {"velocity", sizeof(float) * 3},
    {"mass",     sizeof(float)},
    {"label",    sizeof(char[32])},
};
#define POS WB_COMP_POSITION
#define VEL WB_COMP_VELOCITY
#define MASS WB_COMP_MASS
#define LABEL WB_COMP_LABEL
#define kCompCount (sizeof(kComps)/sizeof(kComps[0]))

#define WB_META_WORLD "wb_world_ptr"
#define SANDBOX_ENV_KEY "wb_sandbox_env"
#define WB_SPACE_CTX_KEY "wb_space_ctx"

/* The space a sandboxed state is attached to, and the gametype whose world the
 * state belongs to. Every space binding call is attributed to `self_mode`, so
 * a mode can declare gates only in its own world. */
typedef struct {
    wb_worldspace *ws;
    char self_mode[WB_MODE_NAME_LEN];
} wb_space_ctx;

/* register the component schema on a world (idempotent per new world) */
void wb_world_setup_comps(wb_world *w) {
    for (uint32_t i = 0; i < kCompCount; i++)
        wb_register_comp(w, kComps[i].name, kComps[i].size);
}

void wb_script_push_entity(lua_State *L, wb_entity e) {
    lua_createtable(L, 0, 2);
    lua_pushinteger(L, e.index); lua_setfield(L, -2, "i");
    lua_pushinteger(L, e.gen);   lua_setfield(L, -2, "g");
}

wb_entity wb_world_spawn_entity(wb_world *w, const char *label,
                                const float pos[3]) {
    if (!w) return (wb_entity){0, 0};
    wb_entity e = wb_entity_new(w);
    float *p = (float *)wb_comp_add(w, e, POS);
    float *v = (float *)wb_comp_add(w, e, VEL);
    float *m = (float *)wb_comp_add(w, e, MASS);
    char *lbl = (char *)wb_comp_add(w, e, LABEL);
    if (p) { p[0] = pos[0]; p[1] = pos[1]; p[2] = pos[2]; }
    if (v) { v[0] = v[1] = v[2] = 0.0f; }
    if (m) *m = 1.0f;
    if (lbl) snprintf(lbl, 32, "%s", label ? label : "anon");
    return e;
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
    wb_entity e = wb_world_spawn_entity(w, label, (const float[3]){0.0f, 0.0f, 0.0f});
    wb_script_push_entity(L, e);
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

lua_State *wb_script_create_state(wb_world *w, wb_worldspace *ws,
                                  const char *self_mode) {
    lua_State *L = luaL_newstate();
    if (!L) return NULL;
    luaL_openlibs(L);
    lua_pushlightuserdata(L, w);
    lua_setfield(L, LUA_REGISTRYINDEX, WB_META_WORLD);
    luaL_newlib(L, kBindings);
    lua_setglobal(L, "wb");
    /* Carry the space context (if any). The bindings themselves are installed
     * by wb_space_bind_install(), so this file never references the
     * worldspace stack. */
    if (ws) wb_script_sandbox_attach_space(L, ws, self_mode);
    return L;
}

static void push_sandbox_global(lua_State *L, const char *name);

int wb_script_step(struct lua_State *L, float dt) {
    push_sandbox_global(L, "step");
    if (lua_isnil(L, -1)) { lua_pop(L, 1); return 0; }
    lua_pushnumber(L, dt);
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        fprintf(stderr, "wb: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return -1;
    }
    return 1;
}

/* ==================================================================== */
/* sandbox (untrusted mod code)                                         */
/* ==================================================================== */

/* Libraries a mod must never reach. Everything else falls through to the real
 * globals via the registry. */
static int sandbox_blocked(const char *k) {
    static const char *blocked[] = {
        "os", "io", "debug", "package", "require",
        "load", "loadfile", "loadstring", "dofile",
        "collectgarbage", "getfenv", "setfenv", "newproxy",
        NULL
    };
    for (int i = 0; blocked[i]; i++)
        if (!strcmp(blocked[i], k)) return 1;
    return 0;
}

/* __index proxy: whitelist through to the real globals, block the rest. */
static int sandbox_index(lua_State *L) {
    const char *k = lua_tostring(L, 2);
    if (k && sandbox_blocked(k)) { lua_pushnil(L); return 1; }
    lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    return 1;
}

/* __newindex: mods may write their own globals, never the host's. */
static int sandbox_newindex(lua_State *L) {
    lua_pushvalue(L, 1);
    lua_pushvalue(L, 2);
    lua_pushvalue(L, 3);
    lua_rawset(L, -3);
    return 0;
}

typedef struct { long budget; long used; int exceeded; } sandbox_state;

#define SANDBOX_STATE_KEY "wb_sandbox_state"

static sandbox_state *sandbox_state_of(lua_State *L) {
    lua_getfield(L, LUA_REGISTRYINDEX, SANDBOX_STATE_KEY);
    sandbox_state *st = (sandbox_state *)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return st;
}

static void sandbox_hook(lua_State *L, lua_Debug *ar) {
    (void)ar;
    sandbox_state *st = sandbox_state_of(L);
    if (!st) return;
    st->used++;
    if (st->budget > 0 && st->used > st->budget) {
        st->exceeded = 1;
        luaL_error(L, "instruction budget exceeded");
    }
}

lua_State *wb_script_create_sandbox_state(wb_world *w, long budget) {
    lua_State *L = luaL_newstate();
    if (!L) return NULL;
    luaL_openlibs(L);

    lua_pushlightuserdata(L, w);
    lua_setfield(L, LUA_REGISTRYINDEX, WB_META_WORLD);

    /* wb.* capability table, visible to mods through the sandbox proxy */
    luaL_newlib(L, kBindings);
    lua_setglobal(L, "wb");

    /* The sandbox _ENV: a fresh table whose __index is a whitelist proxy. */
    lua_newtable(L);
    lua_newtable(L);
    lua_pushcfunction(L, sandbox_index);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, sandbox_newindex);
    lua_setfield(L, -2, "__newindex");
    lua_setmetatable(L, -2);
    lua_setfield(L, LUA_REGISTRYINDEX, SANDBOX_ENV_KEY);

    sandbox_state *st = (sandbox_state *)calloc(1, sizeof *st);
    st->budget = budget;
    lua_pushlightuserdata(L, st);
    lua_setfield(L, LUA_REGISTRYINDEX, SANDBOX_STATE_KEY);

    wb_script_set_budget(L, budget);
    return L;
}

void wb_script_set_budget(lua_State *L, long budget) {
    sandbox_state *st = sandbox_state_of(L);
    if (!st) return;
    st->budget = budget;
    st->used = 0;
    st->exceeded = 0;
    if (budget > 0) lua_sethook(L, sandbox_hook, LUA_MASKCOUNT, 100);
    else            lua_sethook(L, NULL, 0, 0);
}

/* Attach the space to a state (slice 21). The state carries an opaque context
 * -- the worldspace, and the gametype the script belongs to, so every space
 * call is attributed to that mode's own world. `ws` may be NULL, in which case
 * the call is a no-op returning 0 (a state with no space simply has no space
 * bindings). The bindings themselves are installed separately by
 * wb_space_bind_install(), which lives in wb_space_bind.c so this file never
 * references the worldspace stack. */
int wb_script_sandbox_attach_space(lua_State *L, wb_worldspace *ws,
                                   const char *self_mode) {
    if (!L) return -1;
    if (!ws) return 0;

    wb_space_ctx *ctx = (wb_space_ctx *)calloc(1, sizeof *ctx);
    if (!ctx) return -1;
    ctx->ws = ws;
    snprintf(ctx->self_mode, sizeof ctx->self_mode, "%s", self_mode ? self_mode : "");
    lua_pushlightuserdata(L, ctx);
    lua_setfield(L, LUA_REGISTRYINDEX, WB_SPACE_CTX_KEY);
    return 0;
}

/* Read back the space context attached to a state. Returns 0 when one is
 * attached, -1 when there is none. `self_mode` may be NULL. */
int wb_script_space_ctx(lua_State *L, wb_worldspace **ws,
                        char *self_mode, size_t self_mode_sz) {
    if (!L) return -1;
    lua_getfield(L, LUA_REGISTRYINDEX, WB_SPACE_CTX_KEY);
    wb_space_ctx *ctx = (wb_space_ctx *)lua_touserdata(L, -1);
    lua_pop(L, 1);
    if (!ctx || !ctx->ws) return -1;
    if (ws) *ws = ctx->ws;
    if (self_mode && self_mode_sz)
        snprintf(self_mode, self_mode_sz, "%s", ctx->self_mode);
    return 0;
}

/* Run a chunk of untrusted source under the sandbox: load it, swap in the
 * sandbox _ENV, and call it. Returns 0 on success, -1 on error (message on
 * the stack). */
int wb_script_run_sandbox(lua_State *L, const char *src, size_t len,
                          const char *chunkname) {
    if (luaL_loadbuffer(L, src, len, chunkname) != LUA_OK) return -1;
    /* stack: [fn] */
    lua_getfield(L, LUA_REGISTRYINDEX, SANDBOX_ENV_KEY);
    /* stack: [fn, env] */
    if (!lua_setupvalue(L, -2, 1)) { lua_pop(L, 2); return -1; }  /* set _ENV */
    /* stack: [fn] -- lua_setupvalue popped the env */
    sandbox_state *st = sandbox_state_of(L);
    if (st) { st->used = 0; st->exceeded = 0; }
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        fprintf(stderr, "mod: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return -1;
    }
    return 0;
}

/* Push a global from the sandbox env when one is installed, else the real
 * globals. Mod globals live in the sandbox, so this is what the host must use
 * to observe or call into a mod. */
static void push_sandbox_global(lua_State *L, const char *name) {
    lua_getfield(L, LUA_REGISTRYINDEX, SANDBOX_ENV_KEY);
    if (lua_istable(L, -1)) {
        lua_pushstring(L, name);
        lua_rawget(L, -2);
        lua_remove(L, -2);          /* drop the env table, leave the value */
    } else {
        lua_pop(L, 1);
        lua_getglobal(L, name);
    }
}

int wb_script_budget_exceeded(struct lua_State *L) {
    sandbox_state *st = sandbox_state_of(L);
    return st ? st->exceeded : 0;
}

/* Read a numeric global from a sandboxed state (tests / tooling). */
int wb_script_get_sandbox_number(struct lua_State *L, const char *name, double *out) {
    if (!L || !name || !out) return -1;
    push_sandbox_global(L, name);
    if (!lua_isnumber(L, -1)) { lua_pop(L, 1); return -1; }
    *out = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return 0;
}

void wb_physics_tick(wb_world *w, float dt) {
    float drag = 0.995f;
    uint32_t n = wb_world_next_index(w);
    size_t psize = wb_world_comp_elem_size(w, POS);
    size_t vsize = wb_world_comp_elem_size(w, VEL);
    uint8_t *pbase = (uint8_t *)wb_world_comp_data(w, POS);
    uint8_t *vbase = (uint8_t *)wb_world_comp_data(w, VEL);
    for (uint32_t i = 0; i < n; i++) {
        if (!wb_world_slot_alive(w, i)) continue;
        if (!wb_world_comp_present(w, POS, i)) continue;
        if (!wb_world_comp_present(w, VEL, i)) continue;
        float *p = (float *)(pbase + (size_t)i * psize);
        float *v = (float *)(vbase + (size_t)i * vsize);
        for (int c = 0; c < 3; c++) {
            v[c] *= drag;
            p[c] += v[c] * dt;
        }
    }
}

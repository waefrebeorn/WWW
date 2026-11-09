/*
 * wb_lua_host.c -- Standalone engine entrypoint.
 *
 * Loads ONE gamemode script, runs the main loop until the script sets
 * wb.running=false. Builds on wb_script (shared Lua<->core binding).
 *
 * Build: gcc -std=c11 -Ithirdparty/lua5.4 -Isrc/core -Isrc/lua \
 *        src/core/wb_ecs.c src/lua/wb_script.c src/lua/wb_lua_host.c \
 *        every .c under thirdparty/lua5.4 -o wubu_engine -lm
 */
#include "wb_ecs.h"
#include "wb_script.h"
#include <lua.h>
#include <lauxlib.h>
#include <stdio.h>

static void err(const char *msg) { fprintf(stderr, "wb: %s\n", msg); }

int main(int argc, char **argv) {
    const char *script = argc > 1 ? argv[1] : "game/main.lua";

    wb_world *world = wb_world_new();
    if (!world) { err("out of memory"); return 1; }
    wb_world_setup_comps(world);

    lua_State *L = wb_script_create_state(world, NULL, NULL);

    if (luaL_dofile(L, script) != LUA_OK) {
        err(lua_tostring(L, -1));
        lua_pop(L, 1);
        wb_world_destroy(world);
        lua_close(L);
        return 1;
    }

    int guard = 0;
    while (guard++ < 120000) {
        lua_getglobal(L, "wb");
        lua_getfield(L, -1, "running");
        if (lua_isnil(L, -1)) break;
        int running = lua_toboolean(L, -1);
        lua_pop(L, 1);
        if (!running) break;
        lua_pop(L, 1);

        if (wb_script_step(L, 1.0f / 60.0f) < 0) break;

        wb_physics_tick(world, 1.0f / 60.0f);
    }

    wb_world_destroy(world);
    lua_close(L);
    printf("wb: engine exited clean after %d ticks\n", guard - 1);
    return 0;
}

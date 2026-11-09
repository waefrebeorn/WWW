/*
 * wb_exp_test.c -- verify experience containers (slice 7).
 *
 * Two instances of the SAME gamemode spawned at different cells; each owns an
 * independent Lua state + ECS world. The gamemode spawns a FIXED 2 entities
 * at load and grows never -- so instance isolation is unambiguous: a player
 * joined into ONE instance appears only there, and the other is untouched.
 */
#include "wb_exp.h"
#include "wb_ecs.h"
#include <stdio.h>
#include <string.h>

static int g_pass=0, g_fail=0;
#define CHECK(cond,msg) do{ if(cond) g_pass++; else{g_fail++;printf("FAIL: %s (line %d)\n",msg,__LINE__);} }while(0)

int main(void){
    const char *gm = "games/gamemode_demo.lua";
    /* the gamemode spawns 2 load-time mobs per instance */
    const uint32_t LOAD_MOBS = 2;

    wb_exp_cfg cfgA = { .cell = wb_qkey_make(6, 10, 10), .lua_path = gm };
    wb_exp_cfg cfgB = { .cell = wb_qkey_make(6, 20, 20), .lua_path = gm };

    wb_exp *a = wb_exp_spawn(&cfgA, "arena_north");
    wb_exp *b = wb_exp_spawn(&cfgB, "arena_south");
    CHECK(a != NULL, "instance A spawned");
    CHECK(b != NULL, "instance B spawned");
    if (!a || !b) { printf("=== EXP TESTS: %d pass, %d fail ===\n",g_pass,g_fail); return 1; }

    CHECK(strcmp(wb_exp_name(a),"arena_north")==0, "A named correctly");
    CHECK(strcmp(wb_exp_name(b),"arena_south")==0, "B named correctly");
    CHECK(wb_exp_cell(a)==wb_qkey_make(6,10,10) && wb_exp_cell(b)==wb_qkey_make(6,20,20),
          "instances anchored at their cells");
    CHECK(wb_exp_world(a) != wb_exp_world(b), "instances have separate worlds");

    /* step: gamemode step() does nothing, so count should equal load-time mobs */
    uint32_t a0 = wb_exp_step(a, 1.0f/60.0f);
    uint32_t b0 = wb_exp_step(b, 1.0f/60.0f);
    CHECK(a0 == LOAD_MOBS, "A has exactly its 2 load-time mobs");
    CHECK(b0 == LOAD_MOBS, "B has exactly its 2 load-time mobs");

    /* several idle frames: still just the load-time mobs (deterministic) */
    for (int i=0;i<8;i++){ wb_exp_step(a,1.0f/60.0f); wb_exp_step(b,1.0f/60.0f); }
    uint32_t aN = wb_exp_step(a,1.0f/60.0f);
    uint32_t bN = wb_exp_step(b,1.0f/60.0f);
    CHECK(aN == LOAD_MOBS, "A did not grow on its own");
    CHECK(bN == LOAD_MOBS, "B did not grow on its own");

    /* join a player into A only -> A has exactly one more than B */
    int jA = wb_exp_join(a, "player_conan");
    CHECK(jA == 1, "player joined instance A");
    aN = wb_exp_step(a,1.0f/60.0f);
    bN = wb_exp_step(b,1.0f/60.0f);
    CHECK(aN == bN + 1, "joined player lives ONLY in A's world (isolation)");
    CHECK(aN == LOAD_MOBS + 1, "A count = 2 mobs + 1 joined player");

    /* B is completely unaffected */
    CHECK(bN == LOAD_MOBS, "B untouched by A's join");

    /* clean teardown */
    wb_exp_dispose(a);
    wb_exp_dispose(b);
    CHECK(1, "dispose both clean");

    printf("=== EXP TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

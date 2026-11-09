/*
 * wb_space_test.c -- slice 21 gate: the gamemode-facing space.
 *
 * The Yu-Gi-Oh stadium mechanic, exposed to a script. A gamemode's own
 * main.lua declares its stadium gates (wb.add_gate), detects a player walking
 * into one (wb.gate_at), and travels them into another gametype's instance at
 * its landing tag (wb.travel) -- the SAME identity carried across, the empty
 * source world reaped. All of it from inside the untrusted sandbox.
 */
#include "wb_mod.h"
#include "wb_script.h"
#include "wb_worldspace.h"
#include "wb_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static const char *kHub =
    "{\n"
    "  \"name\": \"omniverse-hub\",\n"
    "  \"version\": \"1.0.0\",\n"
    "  \"entry\": \"main.lua\",\n"
    "  \"description\": \"the 1:1 mirror of the universe\",\n"
    "  \"max_players\": 100,\n"
    "  \"teams\": 1\n"
    "}\n";

static const char *kStadium =
    "{\n"
    "  \"name\": \"duel-stadium\",\n"
    "  \"version\": \"1.0.0\",\n"
    "  \"entry\": \"stadium.lua\",\n"
    "  \"description\": \"a stadium game space you step into\",\n"
    "  \"max_players\": 64,\n"
    "  \"teams\": 2,\n"
    "  \"config\": [\n"
    "    {\"name\": \"max_rounds\", \"type\": \"int\", \"min\": 1, \"max\": 99, \"default\": \"3\"}\n"
    "  ]\n"
    "}\n";

static void write_file(const char *path, const char *text) {
    char buf[600];
    snprintf(buf, sizeof buf, "%s", path);
    for (char *p = buf + 1; *p; p++) {
        if (*p == '/' || *p == '\\') { *p = 0; wb_ensure_dir(buf); *p = '/'; }
    }
    FILE *f = fopen(path, "wb");
    if (!f) { printf("cannot write %s\n", path); exit(1); }
    fwrite(text, 1, strlen(text), f);
    fclose(f);
}

/* The hub's gamemode: declares a gate into the stadium, and travels any player
 * that walks into it. This is the whole gametype contract -- no C changes. */
static const char *kHubLua =
    "-- the hub's own script: a stadium gate, declared from Lua\n"
    "spawn = wb.add_spawn(1, 2, 3, 0, 'scripted')\n"
    "nspawns = wb.spawn_count()\n"
    "gate = wb.add_gate('duel-stadium', 'landing', 10, 0, 0, 2)\n"
    "\n"
    "function step(dt)\n"
    "  -- a player stands at (10, 0.5, 0): inside the gate volume\n"
    "  local g = wb.gate_at(10, 0.5, 0)\n"
    "  if g then\n"
    "    local ok, inst, x, y, z = wb.travel(g, 'P1', -1)\n"
    "    travelled = ok and 1 or 0\n"
    "    if ok then\n"
    "      dst_inst = inst\n"
    "      dst_x, dst_y, dst_z = x, y, z\n"
    "    end\n"
    "  end\n"
    "  -- the directory, read from inside the sandbox\n"
    "  local worlds = wb.instances()\n"
    "  n_worlds = #worlds\n"
    "  nspawns = wb.spawn_count()\n"
    "  if n_worlds > 0 then\n"
    "    world0 = worlds[1].gamemode\n"
    "    world0_players = worlds[1].players\n"
    "  end\n"
    "end\n";

static const char *kStadiumLua =
    "-- the stadium's script: records that it is a live world\n"
    "live = 1\n";

int main(void) {
    write_file("build/modes/omniverse-hub/mode.json", kHub);
    write_file("build/modes/duel-stadium/mode.json", kStadium);

    wb_worldspace *ws = wb_worldspace_open();
    CHECK(ws != NULL, "worldspace opens");
    char err[512];

    CHECK(wb_worldspace_register_mode(ws, "build/modes/omniverse-hub", err, sizeof err) == 0,
          "the hub registers");
    CHECK(wb_worldspace_register_mode(ws, "build/modes/duel-stadium", err, sizeof err) == 0,
          "the stadium registers");

    /* spawn points: the hub's arrival, the stadium's tagged landing */
    const float arrival[3] = { 0, 0, 0 };
    const float landing[3] = { 50, 0, 50 };
    CHECK(wb_worldspace_add_spawn(ws, "omniverse-hub", arrival, -1, "arrival") == 0,
          "hub arrival spawn");
    CHECK(wb_worldspace_add_spawn(ws, "duel-stadium", landing, -1, "landing") == 0,
          "stadium landing spawn");

    /* P1 is in the hub world */
    char hub_inst[WB_DIST_ID_LEN]; float hub_pos[3];
    CHECK(wb_worldspace_spawn(ws, "omniverse-hub", NULL, "P1", -1,
                              hub_inst, hub_pos, err, sizeof err) == 0,
          "P1 spawns into the hub");
    CHECK(wb_worldspace_portal_count(ws) == 0, "no gates declared yet (the C side is quiet)");

    /* ---- the hub's gamemode, run as an untrusted sandbox ------------- */
    write_file("build/mods/hub/main.lua", kHubLua);
    write_file("build/mods/hub/manifest.json",
               "{\"id\":\"hub\",\"name\":\"Hub\",\"version\":\"1.0.0\","
               "\"entry\":\"main.lua\",\"author\":\"tester\",\"budget\":1000000}");

    wb_mod_manifest mf;
    CHECK(wb_mod_manifest_load("build/mods/hub/manifest.json", &mf) == 0, "hub manifest loaded");

    wb_world *w = wb_world_new();
    wb_world_setup_comps(w);
    wb_mod *hub = wb_mod_load_space("build/mods/hub", &mf, w, ws, "omniverse-hub");
    CHECK(hub != NULL, "hub gamemode loaded into the sandbox");
    if (hub) {
        /* the script's load-time add_gate ran inside the sandbox */
        double gate = -1;
        CHECK(wb_mod_get_global_number(hub, "gate", &gate) == 0 && gate == 0,
              "the gamemode declared its stadium gate from Lua (gate 0)");
        CHECK(wb_worldspace_portal_count(ws) == 1,
              "the gate is in the space, not just in the script");

        /* ...and add_spawn, on top of the one the host declared in C */
        double spawn = -1, nspawns = -1;
        CHECK(wb_mod_get_global_number(hub, "spawn", &spawn) == 0 && spawn == 1,
              "the gamemode added a spawn point from Lua (index 1)");
        CHECK(wb_mod_get_global_number(hub, "nspawns", &nspawns) == 0 && nspawns == 2,
              "the script sees both its own spawn and the host's");

        /* step: the walk-in is detected, the travel happens */
        CHECK(wb_mod_step(hub, 1.0f / 60.0f) == 1, "the hub's step() ran");
        double travelled = 0;
        CHECK(wb_mod_get_global_number(hub, "travelled", &travelled) == 0 && travelled == 1,
              "a player inside the gate volume was travelled from the script");

        double dx = -1, dy = -1, dz = -1;
        wb_mod_get_global_number(hub, "dst_x", &dx);
        wb_mod_get_global_number(hub, "dst_y", &dy);
        wb_mod_get_global_number(hub, "dst_z", &dz);
        CHECK(dx == 50 && dy == 0 && dz == 50,
              "the player materialized at the stadium's tagged landing");

        /* wb.instances() read from inside the sandbox */
        double n_worlds = -1, w0_players = -1;
        CHECK(wb_mod_get_global_number(hub, "n_worlds", &n_worlds) == 0 && n_worlds == 1,
              "the sandbox sees the live directory (one world)");
        CHECK(wb_mod_get_global_number(hub, "world0_players", &w0_players) == 0 && w0_players == 1,
              "the sandbox sees the stadium world's player count");

        /* identity continuity + source reap */
        wb_dist_registry *reg = wb_worldspace_registry(ws);
        CHECK(wb_dist_registry_count(reg) == 1, "the empty hub world was reaped");
        CHECK(wb_dist_registry_find(reg, hub_inst) == NULL,
              "P1 is no longer in the hub world");
        wb_dist_info infos[8];
        int got = wb_dist_registry_list(reg, infos, 8);
        CHECK(got == 1 && !strcmp(infos[0].gamemode, "duel-stadium"),
              "the remaining world is the stadium");
        CHECK(infos[0].players == 1,
              "the remaining world holds exactly one player");
        wb_dist_instance *dinst = wb_dist_registry_find(reg, infos[0].id);
        CHECK(dinst != NULL && wb_dist_instance_has_player(dinst, "P1"),
              "the SAME identity P1 is in the stadium world");
    }

    /* ---- the stadium's own script runs in its own sandbox ------------ */
    write_file("build/mods/stadium/main.lua", kStadiumLua);
    write_file("build/mods/stadium/manifest.json",
               "{\"id\":\"stadium\",\"name\":\"Stadium\",\"version\":\"1.0.0\","
               "\"entry\":\"main.lua\",\"author\":\"tester\",\"budget\":1000000}");
    wb_mod_manifest smf;
    CHECK(wb_mod_manifest_load("build/mods/stadium/manifest.json", &smf) == 0,
          "stadium manifest loaded");
    wb_mod *stadium = wb_mod_load_space("build/mods/stadium", &smf, w, ws, "duel-stadium");
    CHECK(stadium != NULL, "stadium gamemode loaded");
    if (stadium) {
        double live = 0;
        CHECK(wb_mod_get_global_number(stadium, "live", &live) == 0 && live == 1,
              "the stadium's script runs in its own sandbox");
        /* the stadium declared no gates of its own */
        CHECK(wb_worldspace_portal_count(ws) == 1,
              "the stadium's script added no gates");
    }

    wb_mod_free(hub);
    wb_mod_free(stadium);
    wb_world_destroy(w);
    wb_worldspace_close(ws);
    printf("\n=== SPACE TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

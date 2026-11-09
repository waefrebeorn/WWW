/*
 * wb_worldspace_test.c -- slice 19 gate: the spawn-a-worldspace facade.
 *
 * Proves the whole flow a host uses: register gametypes from directories,
 * declare spawn points, then spawn a player into a gametype and get back an
 * instance plus a world position -- with an unknown gametype and a bad config
 * both refused rather than silently accepted.
 */
#include "wb_worldspace.h"
#include "wb_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static const char *kZoneWars =
    "{\n"
    "  \"name\": \"zone-wars\",\n"
    "  \"version\": \"1.0.0\",\n"
    "  \"author\": \"waefrebeorn\",\n"
    "  \"entry\": \"main.lua\",\n"
    "  \"description\": \"Last team standing\",\n"
    "  \"max_players\": 64,\n"
    "  \"teams\": 2,\n"
    "  \"config\": [\n"
    "    {\"name\": \"max_players\", \"type\": \"int\", \"min\": 2, \"max\": 64, \"default\": \"32\"},\n"
    "    {\"name\": \"friendly_fire\", \"type\": \"bool\", \"default\": \"false\"}\n"
    "  ]\n"
    "}\n";

static const char *kRace =
    "{\n"
    "  \"name\": \"rush-hour\",\n"
    "  \"version\": \"0.1.0\",\n"
    "  \"entry\": \"race.lua\",\n"
    "  \"max_players\": 12,\n"
    "  \"teams\": 1,\n"
    "  \"config\": [\n"
    "    {\"name\": \"laps\", \"type\": \"int\", \"min\": 1, \"max\": 99, \"default\": \"3\"}\n"
    "  ]\n"
    "}\n";

static const char *kBadMode =
    "{ \"name\": \"broken\", \"entry\": \"m.lua\","
    "  \"config\": [ {\"name\": \"x\", \"type\": \"colour\"} ] }\n";

static const char *kDuos =
    "{\n"
    "  \"name\": \"duos\",\n"
    "  \"version\": \"1.0.0\",\n"
    "  \"entry\": \"main.lua\",\n"
    "  \"max_players\": 64,\n"
    "  \"teams\": 2,\n"
    "  \"config\": [\n"
    "    {\"name\": \"max_players\", \"type\": \"int\", \"min\": 2, \"max\": 64, \"default\": \"32\"}\n"
    "  ]\n"
    "}\n";

static void write_file(const char *path, const char *text) {
    /* create every parent directory; wb_ensure_dir only makes one level */
    char buf[600];
    snprintf(buf, sizeof buf, "%s", path);
    for (char *p = buf + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            *p = 0;
            wb_ensure_dir(buf);
            *p = '/';
        }
    }
    FILE *f = fopen(path, "wb");
    if (!f) { printf("cannot write %s\n", path); exit(1); }
    fwrite(text, 1, strlen(text), f);
    fclose(f);
}

int main(void) {
    /* ---- two gametypes on disk -------------------------------------------- */
    write_file("build/modes/zone-wars/mode.json", kZoneWars);
    write_file("build/modes/rush-hour/mode.json", kRace);
    write_file("build/modes/broken/mode.json", kBadMode);
    write_file("build/modes/duos/mode.json", kDuos);

    wb_worldspace *ws = wb_worldspace_open();
    CHECK(ws != NULL, "worldspace opens");

    char err[512];
    CHECK(wb_worldspace_register_mode(ws, "build/modes/zone-wars", err, sizeof err) == 0,
          "zone-wars registers");
    CHECK(wb_worldspace_register_mode(ws, "build/modes/rush-hour", err, sizeof err) == 0,
          "rush-hour registers");
    CHECK(wb_worldspace_register_mode(ws, "build/modes/duos", err, sizeof err) == 0,
          "duos registers");
    CHECK(wb_worldspace_mode_count(ws) == 3, "three gametypes registered");

    CHECK(wb_worldspace_register_mode(ws, "build/modes/broken", err, sizeof err) != 0,
          "a broken manifest is refused");
    CHECK(strstr(err, "unknown type") != NULL, "the broken manifest's error is reported");

    /* duplicate registration */
    CHECK(wb_worldspace_register_mode(ws, "build/modes/zone-wars", err, sizeof err) != 0,
          "registering the same gametype twice is refused");
    CHECK(wb_worldspace_mode_count(ws) == 3, "the duplicate did not register");

    /* ---- lookups ----------------------------------------------------------- */
    const wb_mode_manifest *zw = wb_worldspace_mode(ws, "zone-wars");
    CHECK(zw != NULL, "zone-wars is found by name");
    CHECK(zw && zw->teams == 2 && zw->max_players == 64,
          "the manifest's teams and max_players are intact");
    CHECK(wb_worldspace_mode(ws, "nope") == NULL, "an unknown gametype is NULL");

    char dir[600];
    CHECK(wb_worldspace_mode_dir(ws, "zone-wars", dir, sizeof dir) == 0,
          "the mode directory is retrievable");
    CHECK(!strcmp(dir, "build/modes/zone-wars"), "the directory is the one registered");

    /* ---- spawn points ------------------------------------------------------ */
    /* Four per team, so repeated spawns and exhaustion are both testable. */
    for (int i = 0; i < 4; i++) {
        float r[3] = { 40.0f + (float)i, 0.0f, 0.0f };
        float b[3] = { -40.0f - (float)i, 0.0f, 0.0f };
        CHECK(wb_worldspace_add_spawn(ws, "zone-wars", r, 0, "red")  == 2 * i,
              "red spawn added");
        CHECK(wb_worldspace_add_spawn(ws, "zone-wars", b, 1, "blue") == 2 * i + 1,
              "blue spawn added");
    }
    const float grid[3] = { 0.0f, 0.0f, 0.0f };
    CHECK(wb_worldspace_add_spawn(ws, "rush-hour", grid, -1, "grid") == 0,
          "grid spawn added");
    CHECK(wb_worldspace_spawn_count(ws, "zone-wars") == 8, "zone-wars has eight spawns");
    CHECK(wb_worldspace_spawn_count(ws, "rush-hour") == 1, "rush-hour has one spawn");
    CHECK(wb_worldspace_add_spawn(ws, "nope", grid, 0, "x") == -1,
          "a spawn for an unknown gametype is refused");

    /* ---- the spawn call ----------------------------------------------------- */
    char inst[WB_DIST_ID_LEN];
    float pos[3] = { -1.0f, -1.0f, -1.0f };
    memset(inst, 0, sizeof inst);

    CHECK(wb_worldspace_spawn(ws, "zone-wars", NULL, "player-1", 0, inst, pos, err, sizeof err) == 0,
          "a player spawns into zone-wars with no config (all defaults)");
    CHECK(inst[0] != 0, "an instance id comes back");
    CHECK(pos[0] == 40.0f && pos[1] == 0.0f && pos[2] == 0.0f,
          "the player spawns at the red base");

    /* the same player id joins the same instance, not a second one */
    char inst2[WB_DIST_ID_LEN];
    float pos2[3];
    memset(inst2, 0, sizeof inst2);
    CHECK(wb_worldspace_spawn(ws, "zone-wars", NULL, "player-1", 0, inst2, pos2, err, sizeof err) == 0,
          "the same player spawns again");
    CHECK(!strcmp(inst, inst2), "the same player stays in the same instance");
    CHECK(pos2[0] == 41.0f, "the second spawn takes the next free red point");

    /* a second player fills the other team's spawn */
    char inst3[WB_DIST_ID_LEN];
    float pos3[3];
    CHECK(wb_worldspace_spawn(ws, "zone-wars", NULL, "player-2", 1, inst3, pos3, err, sizeof err) == 0,
          "a second player spawns");
    CHECK(pos3[0] == -40.0f, "the second player goes to the blue base");

    /* ---- config drives the world -------------------------------------------- */
    const wb_mode_manifest *m = wb_worldspace_mode(ws, "zone-wars");
    wb_mode_config cfg;
    wb_mode_config_defaults(m, &cfg);
    for (int i = 0; i < cfg.nvalues; i++) {
        if (!strcmp(cfg.values[i].name, "max_players"))
            snprintf(cfg.values[i].value, sizeof cfg.values[i].value, "8");
    }

    char inst4[WB_DIST_ID_LEN];
    float pos4[3];
    CHECK(wb_worldspace_spawn(ws, "zone-wars", &cfg, "player-3", 0, inst4, pos4, err, sizeof err) == 0,
          "a spawn with an explicit config succeeds");
    wb_dist_instance *joined = wb_dist_registry_find(wb_worldspace_registry(ws), inst4);
    CHECK(joined != NULL, "the spawned instance is in the registry");

    /* ---- a FRESH instance is sized by the config --------------------------- */
    /* zone-wars already had an instance, so it kept its capacity. duos is new,
     * so this is the first chance to see the config drive the sizing. */
    const wb_mode_manifest *dm = wb_worldspace_mode(ws, "duos");
    wb_mode_config dcfg;
    wb_mode_config_defaults(dm, &dcfg);
    for (int i = 0; i < dcfg.nvalues; i++) {
        if (!strcmp(dcfg.values[i].name, "max_players"))
            snprintf(dcfg.values[i].value, sizeof dcfg.values[i].value, "4");
    }
    const float dgrid[3] = { 1.0f, 2.0f, 3.0f };
    CHECK(wb_worldspace_add_spawn(ws, "duos", dgrid, -1, "pad") == 0,
          "duos spawn added");
    char dinst[WB_DIST_ID_LEN];
    float dpos[3];
    CHECK(wb_worldspace_spawn(ws, "duos", &dcfg, "duo-1", 0, dinst, dpos, err, sizeof err) == 0,
          "duos spawns with a config");
    wb_dist_instance *di = wb_dist_registry_find(wb_worldspace_registry(ws), dinst);
    CHECK(di != NULL, "the duos instance is in the registry");
    CHECK(di && wb_dist_instance_capacity(di) == 4,
          "a new instance's capacity comes from the config, not the script default");

    /* a bad config is refused, not silently clamped */
    wb_mode_config bad;
    wb_mode_config_defaults(m, &bad);
    for (int i = 0; i < bad.nvalues; i++) {
        if (!strcmp(bad.values[i].name, "max_players"))
            snprintf(bad.values[i].value, sizeof bad.values[i].value, "9999");
    }
    char inst5[WB_DIST_ID_LEN];
    float pos5[3];
    memset(inst5, 0, sizeof inst5);
    CHECK(wb_worldspace_spawn(ws, "zone-wars", &bad, "player-4", 0, inst5, pos5, err, sizeof err) != 0,
          "an out-of-range config is refused at spawn time");
    CHECK(strstr(err, "outside") != NULL, "the config error is reported");
    CHECK(inst5[0] == 0, "a refused spawn hands back no instance id");

    /* ---- unknown gametype ---------------------------------------------------- */
    char inst6[WB_DIST_ID_LEN];
    float pos6[3];
    memset(inst6, 0, sizeof inst6);
    CHECK(wb_worldspace_spawn(ws, "nope", NULL, "player-5", 0, inst6, pos6, err, sizeof err) != 0,
          "an unknown gametype is refused");
    CHECK(strstr(err, "unknown gametype") != NULL, "the unknown-mode error is reported");

    /* ---- exhaustion ---------------------------------------------------------- */
    /* drain the team-0 spawns; the next spawn must be refused */
    int spawned = 0;
    for (int i = 0; i < 20; i++) {
        char tmp_inst[WB_DIST_ID_LEN];
        float tmp_pos[3];
        memset(tmp_inst, 0, sizeof tmp_inst);
        if (wb_worldspace_spawn(ws, "zone-wars", NULL, "filler", 0,
                                tmp_inst, tmp_pos, err, sizeof err) == 0)
            spawned++;
    }
    CHECK(spawned == 1,
          "only the last free team-0 point is handed out; the rest are refused");

    /* team 1 still has room */
    char inst8[WB_DIST_ID_LEN];
    float pos8[3];
    memset(inst8, 0, sizeof inst8);
    CHECK(wb_worldspace_spawn(ws, "zone-wars", NULL, "late-blue", 1,
                              inst8, pos8, err, sizeof err) == 0,
          "a team-1 player still spawns after team 0 is full");
    CHECK(pos8[0] == -41.0f, "the late player takes the next blue point");

    /* ---- other gametype still works ------------------------------------------ */
    char inst7[WB_DIST_ID_LEN];
    float pos7[3];
    CHECK(wb_worldspace_spawn(ws, "rush-hour", NULL, "racer", 0, inst7, pos7, err, sizeof err) == 0,
          "a different gametype still spawns after zone-wars is full");
    CHECK(pos7[0] == 0.0f, "the racer spawns on the grid");

    /* ---- NULL tolerance ------------------------------------------------------- */
    CHECK(wb_worldspace_mode_count(NULL) == 0, "a NULL worldspace has no modes");
    CHECK(wb_worldspace_mode(NULL, "x") == NULL, "a NULL worldspace finds nothing");
    CHECK(wb_worldspace_register_mode(NULL, "x", err, sizeof err) != 0,
          "registering on a NULL worldspace fails");
    wb_worldspace_close(NULL);

    wb_worldspace_close(ws);
    printf("\n=== WORLDSPACE TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

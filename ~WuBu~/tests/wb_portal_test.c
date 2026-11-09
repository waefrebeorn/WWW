/*
 * wb_portal_test.c -- slice 20 gate: the Yu-Gi-Oh stadium mechanic.
 *
 * The hub world is a 1:1 mirror-slice of the universe; a stadium is a game
 * space a player STEPS INTO. This proves the whole travel flow: a player is in
 * the hub instance, walks into a stadium gate (a portal volume), is detected
 * inside it, and travels -- detaching from every world they were in and
 * materializing into the destination gamemode's instance at its tagged landing
 * spawn, with the SAME player identity carried across (the OASIS travel
 * contract). Empty source worlds are reaped.
 */
#include "wb_worldspace.h"
#include "wb_dist.h"
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

    /* spawn points: the hub's arrival zone, the stadium's landing + generic.
     * Each gametype has its own spawn set, so the stadium's indices start at 0. */
    const float arrival[3] = { 0, 0, 0 };
    const float landing[3] = { 50, 0, 50 }, generic[3] = { 55, 1, 51 };
    CHECK(wb_worldspace_add_spawn(ws, "omniverse-hub", arrival, -1, "arrival") == 0,
          "hub arrival spawn");
    CHECK(wb_worldspace_add_spawn(ws, "duel-stadium", landing, -1, "landing") == 0,
          "stadium landing spawn");
    CHECK(wb_worldspace_add_spawn(ws, "duel-stadium", generic, -1, "") == 1,
          "stadium generic spawn");

    /* the player picks a game space and is in the hub instance */
    char hub_inst[WB_DIST_ID_LEN]; float hub_pos[3];
    CHECK(wb_worldspace_spawn(ws, "omniverse-hub", NULL, "P1", -1,
                              hub_inst, hub_pos, err, sizeof err) == 0,
          "P1 spawns into the hub");
    CHECK(hub_pos[0] == 0 && hub_pos[1] == 0 && hub_pos[2] == 0,
          "P1 is at the hub arrival point");

    /* the hub has a stadium gate into duel-stadium, landing at its tag */
    const float gate[3] = { 10, 0, 0 };
    int pid = wb_worldspace_add_portal(ws, "omniverse-hub", gate, 2.0f,
                                       "duel-stadium", "landing");
    CHECK(pid == 0, "the stadium gate is portal 0");
    CHECK(wb_worldspace_portal_count(ws) == 1, "one stadium gate declared");

    /* walk-in detection (the OASIS walk-through) */
    const float inside[3] = { 10, 0.5f, 0 };
    wb_worldspace_portal hit;
    CHECK(wb_worldspace_portal_at(ws, "omniverse-hub", inside, &hit) == 1,
          "a player inside the gate volume is detected");
    CHECK(!strcmp(hit.dst_gamemode, "duel-stadium") && !strcmp(hit.dst_tag, "landing"),
          "the gate points at the stadium landing");
    CHECK(wb_worldspace_portal_at(ws, "omniverse-hub", (float[3]){100,0,0}, NULL) == 0,
          "a player far from the gate is not travelling");
    CHECK(wb_worldspace_portal_at(ws, "duel-stadium", inside, NULL) == 0,
          "the gate is not reachable from the wrong source world");

    /* THE travel: detach everywhere, materialize at the landing */
    char dst_inst[WB_DIST_ID_LEN]; float dst_pos[3];
    CHECK(wb_worldspace_travel(ws, pid, "P1", NULL, -1, dst_inst, dst_pos,
                               err, sizeof err) == 0,
          "P1 travels the stadium gate");

    wb_dist_registry *reg = wb_worldspace_registry(ws);
    CHECK(wb_dist_registry_count(reg) == 1, "empty source world is reaped after travel");
    CHECK(dst_pos[0] == 50 && dst_pos[1] == 0 && dst_pos[2] == 50,
          "P1 materializes at the stadium's tagged landing");
    wb_dist_instance *dinst = wb_dist_registry_find(reg, dst_inst);
    CHECK(dinst != NULL && wb_dist_instance_has_player(dinst, "P1"),
          "the same identity P1 is in the stadium instance (identity continuity)");
    CHECK(wb_dist_registry_find(reg, hub_inst) == NULL,
          "the hub world no longer holds P1");
    CHECK(strcmp(dst_inst, hub_inst) != 0, "the stadium is a different world than the hub");

    /* travel with an invalid portal refuses cleanly */
    float tmp[3];
    CHECK(wb_worldspace_travel(ws, 7, "P2", NULL, -1, dst_inst, tmp, err, sizeof err) != 0,
          "a bad gate index is refused");
    CHECK(strstr(err, "portal") != NULL, "the refusal explains the portal");

    wb_worldspace_close(ws);
    printf("\n=== PORTAL TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

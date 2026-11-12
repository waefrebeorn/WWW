/*
 * wb_host_test.c -- slice 22 gate: the gametype host.
 *
 * The first end-to-end test of the production path. A directory of gametype
 * packages is scanned, every mode.json is registered along with its DECLARED
 * spawn points, the named mode's entry script runs with its space bindings,
 * the player is materialized in that mode's world, and the mode then walks the
 * player into a stadium gate and travels them into another gametype's
 * instance -- the same identity carried across, the empty source reaped.
 *
 * This is the flow `wubu_engine.exe <modes-root> <gametype>` runs.
 */
#include "wb_host.h"
#include "wb_script.h"
#include "wb_worldspace.h"
#include "wb_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

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

/* The hub: declares a rift into the stadium and walks the player into it. */
static const char *kHubJson =
    "{\n"
    "  \"name\": \"omniverse-hub\",\n"
    "  \"version\": \"1.0.0\",\n"
    "  \"entry\": \"main.lua\",\n"
    "  \"max_players\": 100,\n"
    "  \"teams\": 1,\n"
    "  \"spawns\": [ { \"pos\": [0, 0, 0], \"team\": -1, \"tag\": \"arrival\" } ]\n"
    "}\n";

static const char *kHubLua =
    "wb.add_gate('duel-stadium', 'landing', 10, 0, 0, 2)\n"
    "local RIFT = { 10, 0, 0 }\n"
    "local ticks = 0\n"
    "function step(dt)\n"
    "  ticks = ticks + 1\n"
    "  local p = wb.get_pos(wb.player)\n"
    "  local dx, dz = RIFT[1] - p[1], RIFT[3] - p[3]\n"
    "  local dist = math.sqrt(dx * dx + dz * dz)\n"
    "  if dist > 0.25 then\n"
    "    wb.set_vel(wb.player, { dx / dist * 4.0, 0, dz / dist * 4.0 })\n"
    "    return\n"
    "  end\n"
    "  wb.set_vel(wb.player, { 0, 0, 0 })\n"
    "  local g = wb.gate_at(p[1], p[2], p[3])\n"
    "  if g then\n"
    "    local ok = wb.travel(g, wb.player_id, -1)\n"
    "    if ok then wb.running = false end\n"
    "  end\n"
    "end\n";

/* The stadium: its landing tag is what the hub's rift points at. */
static const char *kStadiumJson =
    "{\n"
    "  \"name\": \"duel-stadium\",\n"
    "  \"version\": \"1.0.0\",\n"
    "  \"entry\": \"main.lua\",\n"
    "  \"max_players\": 64,\n"
    "  \"teams\": 2,\n"
    "  \"spawns\": [ { \"pos\": [50, 0, 50], \"team\": -1, \"tag\": \"landing\" } ],\n"
    "  \"config\": [\n"
    "    { \"name\": \"objective_rule\", \"type\": \"int\", \"min\": 0, \"max\": 3, \"default\": \"1\" },\n"
    "    { \"name\": \"objective_target\", \"type\": \"int\", \"min\": 1, \"max\": 100, \"default\": \"10\" },\n"
    "    { \"name\": \"objective_time_limit\", \"type\": \"float\", \"min\": 0, \"max\": 3600, \"default\": \"120\" },\n"
    "    { \"name\": \"objective_sides\", \"type\": \"int\", \"min\": 2, \"max\": 8, \"default\": \"2\" },\n"
    "    { \"name\": \"hint\", \"type\": \"string\", \"default\": \"WASD TO MOVE\" },\n"
    "    { \"name\": \"hint_seconds\", \"type\": \"float\", \"min\": 0, \"max\": 600, \"default\": \"12\" }\n"
    "  ]\n"
    "}\n";

static const char *kStadiumLua =
    "function step(dt)\n"
    "  wb.running = false\n"
    "end\n";

/* A mode whose player walks at a constant speed: the simplest thing that makes
 * a body actually move, which is what render interpolation has to be tested
 * against -- a stationary body interpolates to itself. */
static const char *kDriftJson =
    "{\n"
    "  \"name\": \"drift-stadium\",\n"
    "  \"version\": \"1.0.0\",\n"
    "  \"entry\": \"main.lua\",\n"
    "  \"max_players\": 4,\n"
    "  \"teams\": 1,\n"
    "  \"spawns\": [ { \"pos\": [0, 0, 0], \"team\": -1, \"tag\": \"landing\" } ]\n"
    "}\n";

static const char *kDriftLua =
    "function step(dt)\n"
    "  wb.set_vel(wb.player, { 2, 0, 0 })\n"
    "end\n";

/* A mode whose script does nothing at all: the ONLY thing that can move the
 * player is the input. That is what makes a record/replay test meaningful -- the
 * recorded actions are the whole cause of the run. */
static const char *kWalkJson =
    "{\n"
    "  \"name\": \"walk-stadium\",\n"
    "  \"version\": \"1.0.0\",\n"
    "  \"entry\": \"main.lua\",\n"
    "  \"max_players\": 4,\n"
    "  \"teams\": 1,\n"
    "  \"spawns\": [ { \"pos\": [0, 0, 0], \"team\": -1, \"tag\": \"landing\" } ]\n"
    "}\n";

static const char *kWalkLua =
    "function step(dt)\n"
    "end\n";

int main(void) {
    write_file("build/host/omniverse-hub/mode.json", kHubJson);
    write_file("build/host/omniverse-hub/main.lua", kHubLua);
    write_file("build/host/duel-stadium/mode.json", kStadiumJson);
    write_file("build/host/duel-stadium/main.lua", kStadiumLua);
    write_file("build/host/drift-stadium/mode.json", kDriftJson);
    write_file("build/host/drift-stadium/main.lua", kDriftLua);
    write_file("build/host/walk-stadium/mode.json", kWalkJson);
    write_file("build/host/walk-stadium/main.lua", kWalkLua);

    char err[1024];

    /* ---- the happy path -------------------------------------------------- */
    wb_host *h = wb_host_open("build/host", "omniverse-hub", "P1", -1,
                              err, sizeof err);
    CHECK(h != NULL, "the host opens a directory of gametypes");
    if (!h) {
        printf("host error: %s\n", err);
        printf("\n=== HOST TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
        return 1;
    }

    CHECK(strcmp(wb_host_mode(h), "omniverse-hub") == 0,
          "the player is in the requested gametype");
    CHECK(wb_host_instance(h)[0] != 0, "the player is in a live instance");

    /* both gametypes under the root were registered */
    CHECK(wb_worldspace_mode_count(wb_host_space(h)) == 4,
          "every mode.json under the root was registered");

    /* the DECLARED spawn points were installed before any script ran */
    CHECK(wb_worldspace_spawn_count(wb_host_space(h), "omniverse-hub") == 1,
          "the hub's declared arrival spawn was installed");
    CHECK(wb_worldspace_spawn_count(wb_host_space(h), "duel-stadium") == 1,
          "the stadium's declared landing spawn was installed");

    /* the player is a real body in the mode's world, at the arrival point */
    CHECK(wb_entity_valid(wb_host_world(h), wb_host_player(h)),
          "the player was materialized as an entity in the mode's world");
    CHECK(wb_entity_count(wb_host_world(h)) == 1,
          "the mode's world holds exactly the player");

    /* the mode declared its own gate from the script */
    CHECK(wb_worldspace_portal_count(wb_host_space(h)) == 1,
          "the mode's script declared its stadium gate");

    const char *hub_instance = wb_host_instance(h);

    /* ---- run the mode until it hands off --------------------------------- */
    int ticks = 0;
    while (wb_host_running(h) && ticks < 5000) {
        if (wb_host_step(h, 1.0f / 60.0f) < 0) {
            CHECK(0, "the mode's step() ran without error");
            break;
        }
        wb_physics_tick(wb_host_world(h), 1.0f / 60.0f);
        ticks++;
    }
    CHECK(ticks > 1, "the mode stepped");
    CHECK(!wb_host_running(h), "the mode set wb.running = false after the handoff");

    /* ---- the travel actually happened ------------------------------------- */
    wb_dist_registry *reg = wb_worldspace_registry(wb_host_space(h));
    CHECK(wb_dist_registry_count(reg) == 1,
          "the empty source world was reaped after the handoff");
    CHECK(wb_dist_registry_find(reg, hub_instance) == NULL,
          "the hub instance no longer holds the player");

    wb_dist_info infos[8];
    int got = wb_dist_registry_list(reg, infos, 8);
    CHECK(got == 1 && !strcmp(infos[0].gamemode, "duel-stadium"),
          "the remaining world is the stadium");
    CHECK(infos[0].players == 1, "the stadium world holds exactly one player");

    wb_dist_instance *dinst = wb_dist_registry_find(reg, infos[0].id);
    CHECK(dinst != NULL && wb_dist_instance_has_player(dinst, "P1"),
          "the SAME identity P1 is in the stadium instance");

    wb_host_close(h);

    /* ---- refusal paths ---------------------------------------------------- */
    h = wb_host_open("build/host", "nope", "P1", -1, err, sizeof err);
    CHECK(h == NULL, "an unknown gametype is refused");
    CHECK(strstr(err, "not one of") != NULL, "the refusal names the registered set");

    h = wb_host_open("build/does-not-exist", "omniverse-hub", "P1", -1,
                     err, sizeof err);
    CHECK(h == NULL, "a missing modes directory is refused");

    h = wb_host_open("build/host", "", "P1", -1, err, sizeof err);
    CHECK(h == NULL, "an empty gametype name is refused");

    /* ---- the objective the mode declares -----------------------------------
     * duel-stadium declares one in its mode.json; omniverse-hub does not. The
     * host's job is to install it, step it on the sim clock, stop the match when
     * it resolves, and answer the script's reads. */
    h = wb_host_open("build/host", "duel-stadium", "P1", -1, err, sizeof err);
    CHECK(h != NULL, "the stadium opens with its objective");
    if (h) {
        CHECK(wb_host_objective(h) != NULL,
              "the mode's declared objective is installed");
        CHECK(wb_host_objective_state(h) == WB_OBJ_ONGOING,
              "the objective starts ongoing");
        CHECK(wb_host_objective_state_name(h) != NULL &&
              strcmp(wb_host_objective_state_name(h), "ongoing") == 0,
              "the state has a name");
        CHECK(wb_host_objective_clock(h) > 0.0,
              "the objective has time on the clock");

        /* Scoring runs through the host, and a finished match ends the host. */
        wb_host_score(h, 1, 1);
        CHECK(wb_host_objective_score_of(h, 1) == 1,
              "a point scored through the host is recorded");
        CHECK(wb_host_objective_state(h) == WB_OBJ_ONGOING,
              "one point is not the target");

        /* The host stops when the objective resolves, whatever the script says:
         * the match clock is not the script's business. */
        wb_host_score(h, 1, 9);
        CHECK(wb_host_objective_state(h) == WB_OBJ_WON,
              "the target resolves the objective");
        CHECK(wb_host_objective_winner(h) == 1, "the scoring side won");
        CHECK(wb_host_running(h) == 0,
              "a resolved objective stops the host");

        /* And a bad objective is refused rather than half-installed. */
        CHECK(wb_host_set_objective(h, 99, 2, 1, 0.0) == -1,
              "an unknown rule is refused");

        /* Rendering: a host with an objective draws the match panel into its
         * frame, so the score is visible rather than merely printed. */
        static uint32_t px[640 * 480];
        wb_framebuffer fb = { px, 640, 480 };
        wb_host_render(h, &fb);
        int ink = 0;
        for (int i = 0; i < 640 * 480; i++) if (px[i]) ink++;
        CHECK(ink > 0, "a frame of the world has content");
        CHECK(px[12 * 640 + 14] == 0xFFF2C0,
              "the HUD panel is drawn into the frame");

        /* Onboarding: the hint comes from the mode's own manifest and goes on
         * screen while the match is young. */
        CHECK(wb_host_hint(h) != NULL && strcmp(wb_host_hint(h), "WASD TO MOVE") == 0,
              "the mode's hint is read from its manifest");
        CHECK(wb_host_hint_visible(h) == 1,
              "the hint is up at the start of the match");
        CHECK(wb_host_elapsed(h) == 0.0, "no sim time has passed yet");
        wb_host_step(h, (float)WB_HOST_FIXED_DT);
        CHECK(wb_host_elapsed(h) > 0.0, "sim time advances with the steps");

        /* Animation: rendering is pure, and the walk cycle advances with the sim
         * step rather than the frame -- a render must not be able to move it. */
        static uint32_t r1[640 * 480], r2[640 * 480], r3[640 * 480];
        wb_framebuffer f1 = { r1, 640, 480 };
        wb_framebuffer f2 = { r2, 640, 480 };
        wb_framebuffer f3 = { r3, 640, 480 };
        wb_host_render(h, &f1);
        wb_host_render(h, &f2);
        CHECK(memcmp(r1, r2, sizeof r1) == 0,
              "rendering twice without a step is byte-identical");
        for (int i = 0; i < 5; i++) wb_host_step(h, (float)WB_HOST_FIXED_DT);
        wb_host_render(h, &f3);
        CHECK(memcmp(r1, r3, sizeof r1) != 0,
              "a sim step moves what the frame shows");
        wb_host_close(h);
    }

    /* A mode with no objective behaves exactly as before: no clock, no result,
     * and the host keeps running until the script says stop. */
    h = wb_host_open("build/host", "omniverse-hub", "P1", -1, err, sizeof err);
    CHECK(h != NULL, "the hub opens");
    if (h) {
        CHECK(wb_host_objective(h) == NULL, "a mode without an objective has none");
        CHECK(wb_host_objective_state(h) == WB_OBJ_ONGOING,
              "with no objective there is nothing to resolve");
        CHECK(wb_host_objective_winner(h) == -1, "no winner without an objective");
        CHECK(strcmp(wb_host_objective_state_name(h), "none") == 0,
              "the state name says none");
        CHECK(wb_host_running(h) == 1, "the host still runs");

        /* No objective, no panel: the hub's frame must not carry match ink. */
        static uint32_t px2[640 * 480];
        wb_framebuffer fb2 = { px2, 640, 480 };
        wb_host_render(h, &fb2);
        CHECK(px2[12 * 640 + 14] != 0xFFF2C0,
              "a mode without an objective draws no match panel");
        CHECK(wb_host_hint(h) == NULL, "a mode with no hint declares none");
        CHECK(wb_host_hint_visible(h) == 0, "no hint is shown for such a mode");
        /* A mode with no objective still has a clock to put things on. */
        CHECK(wb_host_elapsed(h) == 0.0, "the hub starts at zero");
        wb_host_close(h);
    }

    /* ---- render interpolation (LP-04) -------------------------------------
     * The frame is drawn BETWEEN simulation steps. A frame time of one and a
     * half steps buys one whole step and leaves a half-step remainder, so the
     * body must be drawn halfway between where it was and where it is. Without
     * this the figure steps at the sim rate and a faster display judders. */
    h = wb_host_open("build/host", "drift-stadium", "P1", -1, err, sizeof err);
    CHECK(h != NULL, "the drift mode opens");
    if (h) {
        /* Two whole steps to establish a previous state. A whole step leaves a
         * rounding residue rather than exactly zero, so the drawn position here
         * is the previous step's -- which is exactly the endpoint the
         * interpolation starts from. */
        wb_host_step(h, (float)WB_HOST_FIXED_DT);
        wb_host_step(h, (float)WB_HOST_FIXED_DT);
        double prev[3], mid[3], sim[3];
        wb_host_body_render_pos(h, wb_host_player(h).index, prev);

        wb_host_step(h, (float)(1.5 * WB_HOST_FIXED_DT));
        CHECK(wb_host_render_alpha(h) > 0.4f &&
              wb_host_render_alpha(h) < 0.6f,
              "a one-and-a-half-step frame leaves a half-step alpha");
        wb_host_body_render_pos(h, wb_host_player(h).index, mid);
        wb_host_player_pos(h, sim);

        double moved = 0.0;
        for (int k = 0; k < 3; k++)
            moved += (sim[k] - prev[k]) * (sim[k] - prev[k]);
        CHECK(moved > 0.0, "the player moved between the two states");

        /* The drawn position lies ON the segment between the two states: not
         * past either end, and not off the line. */
        double worst = 0.0;
        for (int k = 0; k < 3; k++) {
            double lo = prev[k], hi = sim[k];
            if (lo > hi) { double t = lo; lo = hi; hi = t; }
            double v = mid[k];
            if (v < lo - 1e-4) v = lo - v;
            else if (v > hi + 1e-4) v = v - hi;
            else v = 0.0;
            if (v > worst) worst = v;
        }
        CHECK(worst < 1e-4,
              "the drawn position lies between the two simulation states");

        /* And it is not merely the simulated position: interpolation moved it
         * off the endpoint. */
        double lag = 0.0;
        for (int k = 0; k < 3; k++)
            lag += (mid[k] - sim[k]) * (mid[k] - sim[k]);
        CHECK(lag > 1e-6,
              "the drawn position is not the simulated position");

        /* A body with no previous state -- the very first step -- is drawn
         * where it is rather than interpolated out of a stale slot. */
        wb_host_close(h);
        h = wb_host_open("build/host", "drift-stadium", "P1", -1, err, sizeof err);
        CHECK(h != NULL, "the drift mode reopens");
        if (h) {
            double fresh[3], at[3];
            wb_host_body_render_pos(h, wb_host_player(h).index, fresh);
            wb_host_player_pos(h, at);
            CHECK(fresh[0] == at[0] && fresh[1] == at[1] && fresh[2] == at[2],
                  "a body with no previous state is drawn where it is");
            wb_host_close(h);
        }
    }

    /* ---- record / replay (LP-87) ------------------------------------------ */
    /* A run is recorded as the actions each step applied and the digest each
     * step produced. A replay re-runs those actions and compares, so a run can be
     * REPRODUCED rather than merely observed -- and the first divergent tick is
     * the difference between "the checksum disagreed" and "here is where". */
    h = wb_host_open("build/host", "walk-stadium", "P1", -1, err, sizeof err);
    CHECK(h != NULL, "the walk mode opens");
    if (h) {
        wb_replay log;
        CHECK(wb_replay_init(&log, 256) == 0, "the replay log is created");
        CHECK(wb_host_record_start(h, &log) == 0, "recording starts");
        wb_host_input_key_down(h, 'W');            /* the whole cause of the run */
        for (int i = 0; i < 120; i++) wb_host_step(h, (float)WB_HOST_FIXED_DT);
        CHECK(log.n == 120, "every fixed step was recorded");
        CHECK(wb_replay_verify(&log) == 1, "the recorded log verifies");
        double moved[3];
        wb_host_player_pos(h, moved);
        CHECK(moved[0] != 0.0 || moved[1] != 0.0,
              "the recorded input actually moved the player");
        wb_host_close(h);

        /* A replay of the untouched log reproduces the run exactly. */
        h = wb_host_open("build/host", "walk-stadium", "P1", -1, err, sizeof err);
        CHECK(h != NULL, "the walk mode reopens for the replay");
        if (h) {
            CHECK(wb_host_replay_start(h, &log) == 0, "the replay starts");
            for (int i = 0; i < 120; i++) wb_host_step(h, (float)WB_HOST_FIXED_DT);
            CHECK(wb_host_replay_diverged(h) == 0,
                  "a replay of an untouched log does not diverge");
            wb_host_close(h);
        }

        /* Tamper with one recorded digest. This stands in for an engine that
         * computes a different state for the same inputs: the log is intact and
         * the run no longer reproduces. The replay must LOCALIZE it. */
        wb_replay_step st;
        memcpy(&st, log.frames[60].data, sizeof st);
        st.digest ^= 1u;
        memcpy(log.frames[60].data, &st, sizeof st);

        h = wb_host_open("build/host", "walk-stadium", "P1", -1, err, sizeof err);
        CHECK(h != NULL, "the walk mode reopens for the tampered replay");
        if (h) {
            CHECK(wb_host_replay_start(h, &log) == 0, "the tampered replay starts");
            for (int i = 0; i < 120; i++) wb_host_step(h, (float)WB_HOST_FIXED_DT);
            CHECK(wb_host_replay_diverged(h) == 1, "a tampered log diverges");
            CHECK(wb_host_replay_divergence_tick(h) == 60,
                  "the divergence is localized to the tampered tick");
            wb_host_close(h);
        }

        /* And a log that ends early is itself a divergence, not a silent stop. */
        log.n = 30;
        h = wb_host_open("build/host", "walk-stadium", "P1", -1, err, sizeof err);
        CHECK(h != NULL, "the walk mode reopens for the short replay");
        if (h) {
            CHECK(wb_host_replay_start(h, &log) == 0, "the short replay starts");
            for (int i = 0; i < 120; i++) wb_host_step(h, (float)WB_HOST_FIXED_DT);
            CHECK(wb_host_replay_diverged(h) == 1,
                  "a log that ends early diverges");
            CHECK(wb_host_replay_divergence_tick(h) == 30,
                  "the divergence is localized to the end of the log");
            wb_host_close(h);
        }
        wb_replay_free(&log);
    }

    /* ---- fuzz (LP-87) ------------------------------------------------------ */
    /* The fuzzer's whole value is that the SAME SEED always produces the SAME
     * stream: a failing case must be replayable, or the fuzzer found nothing. So
     * the gate fuzzes a run, checks a second run from the same seed is identical,
     * and replays the log. */
    h = wb_host_open("build/host", "walk-stadium", "P1", -1, err, sizeof err);
    CHECK(h != NULL, "the walk mode opens for fuzzing");
    if (h) {
        wb_replay flog;
        CHECK(wb_replay_init(&flog, 256) == 0, "the fuzz log is created");
        CHECK(wb_host_fuzz_start(h, 12345u, &flog) == 0, "the fuzzer starts");
        for (int i = 0; i < 200; i++) wb_host_step(h, (float)WB_HOST_FIXED_DT);
        CHECK(flog.n == 200, "the fuzzer recorded every step");
        wb_host_close(h);

        /* The same seed again, from scratch, must produce the same stream. */
        h = wb_host_open("build/host", "walk-stadium", "P1", -1, err, sizeof err);
        CHECK(h != NULL, "the walk mode reopens for the second fuzz run");
        if (h) {
            wb_replay flog2;
            wb_replay_init(&flog2, 256);
            wb_host_fuzz_start(h, 12345u, &flog2);
            for (int i = 0; i < 200; i++) wb_host_step(h, (float)WB_HOST_FIXED_DT);
            CHECK(wb_replay_crc(&flog2) == wb_replay_crc(&flog),
                  "the same seed produces the same stream");
            wb_host_close(h);
            wb_replay_free(&flog2);
        }

        /* And the log must replay to the same state. */
        h = wb_host_open("build/host", "walk-stadium", "P1", -1, err, sizeof err);
        CHECK(h != NULL, "the walk mode reopens for the fuzz replay");
        if (h) {
            CHECK(wb_host_replay_start(h, &flog) == 0, "the fuzz replay starts");
            for (uint32_t i = 0; i < flog.n; i++)
                wb_host_step(h, (float)WB_HOST_FIXED_DT);
            CHECK(wb_host_replay_diverged(h) == 0,
                  "a fuzzed run replays without diverging");
            wb_host_close(h);
        }
        wb_replay_free(&flog);
    }

    printf("\n=== HOST TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

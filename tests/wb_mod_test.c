/*
 * wb_mod_test.c -- slice 15 gate: the modding sandbox.
 *
 * Writes real mods to disk and verifies the Luau-style guarantees:
 *   - a mod runs and can use the wb.* capability table
 *   - blocked libraries (os/io/debug/...) read as nil
 *   - two mods cannot see each other's globals (isolation)
 *   - an infinite loop is cut off by the instruction budget
 *   - hot reload picks up a changed script in a FRESH state
 */
#include "wb_mod.h"
#include "wb_script.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static void write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fputs(text, f);
    fclose(f);
}

static void write_mod(const char *dir, const char *manifest, const char *lua) {
    char p[800];
    snprintf(p, sizeof p, "%s/manifest.json", dir);
    write_file(p, manifest);
    snprintf(p, sizeof p, "%s/main.lua", dir);
    write_file(p, lua);
}

static const char *kGoodManifest =
    "{\"id\":\"good\",\"name\":\"Good Mod\",\"version\":\"1.0.0\","
    "\"entry\":\"main.lua\",\"author\":\"tester\",\"budget\":1000000,\"hot_reload\":true}";

static const char *kGoodLua =
    "spawned = 0\n"
    "function step(dt)\n"
    "  spawned = spawned + 1\n"
    "  if spawned == 1 then wb.spawn('crate') end\n"
    "end\n";

int main(void) {
    system("cmd /c if not exist build\\mods\\good mkdir build\\mods\\good 2>nul");
    system("cmd /c if not exist build\\mods\\evil mkdir build\\mods\\evil 2>nul");
    system("cmd /c if not exist build\\mods\\loop mkdir build\\mods\\loop 2>nul");

    /* ---- 1: manifest ----------------------------------------------- */
    write_mod("build/mods/good", kGoodManifest, kGoodLua);
    wb_mod_manifest mf;
    CHECK(wb_mod_manifest_load("build/mods/good/manifest.json", &mf) == 0,
          "manifest loaded");
    CHECK(!strcmp(mf.id, "good") && !strcmp(mf.version, "1.0.0"),
          "manifest fields parsed");
    CHECK(wb_mod_manifest_validate(&mf) == 0, "a complete manifest validates");

    wb_mod_manifest bad = mf;
    bad.entry[0] = 0;
    CHECK(wb_mod_manifest_validate(&bad) != 0, "a manifest with no entry is rejected");
    CHECK(wb_mod_manifest_error() != NULL, "the validation error is reported");

    /* ---- 2: a mod runs and can use wb.* ---------------------------- */
    wb_world *w = wb_world_new();
    wb_world_setup_comps(w);

    wb_mod *good = wb_mod_load("build/mods/good", &mf, w);
    CHECK(good != NULL, "mod loaded");
    if (good) {
        wb_mod_step(good, 1.0/60.0);
        double spawned = -1;
        CHECK(wb_mod_get_global_number(good, "spawned", &spawned) == 0 && spawned == 1,
              "the mod's step() ran and advanced its own state");
        CHECK(wb_entity_count(w) == 1, "the mod used the wb.* capability table");
        CHECK(!wb_mod_budget_exceeded(good), "no budget blow-out on a normal step");
        CHECK(wb_mod_needs_reload(good) == 0, "no reload pending for an unchanged file");
    }

    /* ---- 3: blocked libraries ------------------------------------- */
    /* The mod records whether the dangerous globals are absent. */
    write_mod("build/mods/evil",
              "{\"id\":\"evil\",\"name\":\"Evil\",\"version\":\"1.0.0\","
              "\"entry\":\"main.lua\",\"author\":\"tester\",\"budget\":1000000}",
              "function step(dt)\n"
              "  if (os == nil) and (io == nil) and (debug == nil)\n"
              "     and (package == nil) and (load == nil) and (loadstring == nil)\n"
              "  then blocked = 1 else blocked = 0 end\n"
              "  wb.spawn('x')\n"
              "end\n");
    wb_mod_manifest emf;
    wb_mod_manifest_load("build/mods/evil/manifest.json", &emf);
    wb_mod *evil = wb_mod_load("build/mods/evil", &emf, w);
    CHECK(evil != NULL, "second mod loaded");
    if (evil) {
        wb_mod_step(evil, 1.0/60.0);
        double blocked = 0;
        CHECK(wb_mod_get_global_number(evil, "blocked", &blocked) == 0 && blocked == 1,
              "os/io/debug/package/load/loadstring are all nil inside the sandbox");
    }

    /* ---- 4: isolation between mods -------------------------------- */
    /* good set a global; evil must not be able to see it. */
    if (evil) {
        double leaked = 0;
        int rc = wb_mod_get_global_number(evil, "spawned", &leaked);
        CHECK(rc != 0 || leaked == 0,
              "one mod cannot read another mod's globals");
    }

    /* ---- 5: instruction budget ------------------------------------ */
    write_mod("build/mods/loop",
              "{\"id\":\"loop\",\"name\":\"Loop\",\"version\":\"1.0.0\","
              "\"entry\":\"main.lua\",\"author\":\"tester\",\"budget\":5000}",
              "function step(dt)\n  while true do end\nend\n");
    wb_mod_manifest lmf;
    wb_mod_manifest_load("build/mods/loop/manifest.json", &lmf);
    wb_mod *loop = wb_mod_load("build/mods/loop", &lmf, w);
    CHECK(loop != NULL, "runaway mod loaded");
    if (loop) {
        int rc = wb_mod_step(loop, 1.0/60.0);
        CHECK(rc == -1, "an infinite loop is stopped by the instruction budget");
        CHECK(wb_mod_budget_exceeded(loop), "the budget blow-out is reported");
        CHECK(wb_mod_error(loop) != NULL, "the mod error is reported");
    }

    /* ---- 6: hot reload -------------------------------------------- */
    if (good) {
        /* change the entry script and confirm the hash notices */
        write_file("build/mods/good/main.lua",
                   "spawned = 0\n"
                   "function step(dt)\n"
                   "  spawned = spawned + 10\n"
                   "  if spawned == 10 then wb.spawn('reloaded') end\n"
                   "end\n");
        CHECK(wb_mod_needs_reload(good) == 1, "hot reload detects the changed file");
        CHECK(wb_mod_reload(good) == 0, "hot reload succeeded");
        CHECK(wb_mod_needs_reload(good) == 0, "no reload pending after reloading");
        wb_mod_step(good, 1.0/60.0);
        double spawned = -1;
        wb_mod_get_global_number(good, "spawned", &spawned);
        CHECK(spawned == 10, "the reloaded script is the one now running");
    }

    wb_mod_free(good);
    wb_mod_free(evil);
    wb_mod_free(loop);
    wb_world_destroy(w);

    printf("\n=== MOD TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

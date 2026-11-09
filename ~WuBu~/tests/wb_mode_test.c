/*
 * wb_mode_test.c -- slice 19 gate: gametype manifests + config schema.
 *
 * A gametype is a declared, parameterisable package. This proves the manifest
 * is read, the schema is enforced (types, ranges, required keys), the defaults
 * apply when a config is absent, and the same mode yields different config
 * values without a second script.
 */
#include "wb_mode.h"
#include "wb_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static const char *kModeJson =
    "{\n"
    "  \"name\": \"zone-wars\",\n"
    "  \"version\": \"1.0.0\",\n"
    "  \"author\": \"waefrebeorn\",\n"
    "  \"entry\": \"main.lua\",\n"
    "  \"description\": \"Last team standing in a shrinking zone\",\n"
    "  \"max_players\": 64,\n"
    "  \"teams\": 2,\n"
    "  \"config\": [\n"
    "    {\"name\": \"max_players\", \"type\": \"int\", \"min\": 2, \"max\": 64, \"default\": \"32\"},\n"
    "    {\"name\": \"round_seconds\", \"type\": \"float\", \"min\": 60, \"max\": 3600, \"default\": \"600\"},\n"
    "    {\"name\": \"friendly_fire\", \"type\": \"bool\", \"default\": \"false\"},\n"
    "    {\"name\": \"zone_start_radius\", \"type\": \"float\", \"min\": 50, \"max\": 5000, \"default\": \"800\"},\n"
    "    {\"name\": \"map\", \"type\": \"string\", \"default\": \"island\"}\n"
    "  ],\n"
    "  \"spawns\": [\n"
    "    {\"pos\": [0, 0, 0], \"team\": -1, \"tag\": \"arrival\"},\n"
    "    {\"pos\": [40, 0, 0], \"team\": 0, \"tag\": \"red\"},\n"
    "    {\"pos\": [-40, 0, 0], \"team\": 1, \"tag\": \"blue\"}\n"
    "  ]\n"
    "}\n";

static const char *kBadTypeJson =
    "{ \"name\": \"broken\", \"entry\": \"m.lua\","
    "  \"config\": [ {\"name\": \"x\", \"type\": \"colour\", \"default\": \"red\"} ] }\n";

static const char *kInvertedJson =
    "{ \"name\": \"inverted\", \"entry\": \"m.lua\","
    "  \"config\": [ {\"name\": \"x\", \"type\": \"int\", \"min\": 10, \"max\": 1, \"default\": \"5\"} ] }\n";

static void write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (!f) { printf("cannot write %s\n", path); exit(1); }
    fwrite(text, 1, strlen(text), f);
    fclose(f);
}

int main(void) {
    /* ---- load + validate ------------------------------------------------ */
    write_file("build/test_mode.json", kModeJson);
    wb_mode_manifest m;
    CHECK(wb_mode_manifest_load("build/test_mode.json", &m) == 0, "manifest loads");
    CHECK(m.error[0] == 0, "no load error");

    char err[256];
    CHECK(wb_mode_manifest_validate(&m, err, sizeof err) == 0, "manifest validates");
    CHECK(!strcmp(m.name, "zone-wars"), "name parsed");
    CHECK(!strcmp(m.version, "1.0.0"), "version parsed");
    CHECK(!strcmp(m.entry, "main.lua"), "entry parsed");
    CHECK(m.max_players == 64, "max_players parsed");
    CHECK(m.teams == 2, "teams parsed");
    CHECK(m.ncfgs == 5, "five config entries parsed");
    CHECK(m.cfgs[0].type == WB_MODE_CFG_INT, "cfg 0 is int");
    CHECK(m.cfgs[1].type == WB_MODE_CFG_FLOAT, "cfg 1 is float");
    CHECK(m.cfgs[2].type == WB_MODE_CFG_BOOL, "cfg 2 is bool");
    CHECK(m.cfgs[4].type == WB_MODE_CFG_STRING, "cfg 4 is string");

    /* ---- declared spawn points ------------------------------------------- */
    CHECK(m.nspawns == 3, "three declared spawns parsed");
    CHECK(m.spawns[0].pos[0] == 0.0f && m.spawns[0].pos[1] == 0.0f &&
          m.spawns[0].pos[2] == 0.0f, "spawn 0 position parsed");
    CHECK(m.spawns[0].team == -1, "spawn 0 is any-team");
    CHECK(!strcmp(m.spawns[0].tag, "arrival"), "spawn 0 tag parsed");
    CHECK(m.spawns[1].team == 0 && m.spawns[1].pos[0] == 40.0f,
          "spawn 1 is the red base");
    CHECK(m.spawns[2].team == 1 && m.spawns[2].pos[0] == -40.0f,
          "spawn 2 is the blue base");

    /* ---- entry path resolution ------------------------------------------ */
    char path[600];
    CHECK(wb_mode_entry_path(&m, "gametypes/zone-wars", path, sizeof path) == 0,
          "entry path resolves");
    CHECK(!strcmp(path, "gametypes/zone-wars/main.lua"),
          "entry path is dir + entry");

    /* ---- defaults -------------------------------------------------------- */
    wb_mode_config def;
    wb_mode_config_defaults(&m, &def);
    CHECK(def.nvalues == 5, "defaults fill every schema entry");
    CHECK(wb_mode_config_get_int(&m, &def, "max_players", 0) == 32,
          "int default applies");
    CHECK(wb_mode_config_get_float(&m, &def, "round_seconds", 0.0) == 600.0,
          "float default applies");
    CHECK(wb_mode_config_get_bool(&m, &def, "friendly_fire", 1) == 0,
          "bool default applies");

    /* ---- validation: unknown key ----------------------------------------- */
    wb_mode_config c1;
    wb_mode_config_defaults(&m, &c1);
    snprintf(c1.values[c1.nvalues].name, sizeof c1.values[0].name, "nonsense");
    snprintf(c1.values[c1.nvalues].value, sizeof c1.values[0].value, "1");
    c1.nvalues++;
    CHECK(wb_mode_config_validate(&m, &c1, err, sizeof err) != 0,
          "an unknown config key is rejected");
    CHECK(strstr(err, "unknown config key") != NULL, "unknown key error is reported");

    /* ---- validation: out of range ---------------------------------------- */
    wb_mode_config c2;
    wb_mode_config_defaults(&m, &c2);
    for (int i = 0; i < c2.nvalues; i++)
        if (!strcmp(c2.values[i].name, "max_players"))
            snprintf(c2.values[i].value, sizeof c2.values[i].value, "999");
    CHECK(wb_mode_config_validate(&m, &c2, err, sizeof err) != 0,
          "an out-of-range value is rejected");
    CHECK(strstr(err, "outside") != NULL, "range error names the key and bounds");

    /* ---- validation: unparseable ----------------------------------------- */
    wb_mode_config c3;
    wb_mode_config_defaults(&m, &c3);
    for (int i = 0; i < c3.nvalues; i++)
        if (!strcmp(c3.values[i].name, "round_seconds"))
            snprintf(c3.values[i].value, sizeof c3.values[i].value, "ten");
    CHECK(wb_mode_config_validate(&m, &c3, err, sizeof err) != 0,
          "a non-numeric value is rejected");

    /* ---- validation: bad bool -------------------------------------------- */
    wb_mode_config c4;
    wb_mode_config_defaults(&m, &c4);
    for (int i = 0; i < c4.nvalues; i++)
        if (!strcmp(c4.values[i].name, "friendly_fire"))
            snprintf(c4.values[i].value, sizeof c4.values[i].value, "maybe");
    CHECK(wb_mode_config_validate(&m, &c4, err, sizeof err) != 0,
          "an unparseable bool is rejected");

    /* ---- a valid override ------------------------------------------------ */
    wb_mode_config c5;
    wb_mode_config_defaults(&m, &c5);
    for (int i = 0; i < c5.nvalues; i++) {
        if (!strcmp(c5.values[i].name, "max_players"))
            snprintf(c5.values[i].value, sizeof c5.values[i].value, "16");
        if (!strcmp(c5.values[i].name, "friendly_fire"))
            snprintf(c5.values[i].value, sizeof c5.values[i].value, "true");
    }
    CHECK(wb_mode_config_validate(&m, &c5, err, sizeof err) == 0,
          "a valid override validates");
    CHECK(wb_mode_config_get_int(&m, &c5, "max_players", 0) == 16,
          "the override wins over the default");
    CHECK(wb_mode_config_get_bool(&m, &c5, "friendly_fire", 0) == 1,
          "bool override parses");

    /* ---- one mode, two configs ------------------------------------------- */
    /* The whole point: same gametype, different world. The schema floor is 2,
     * so "duos" is a different world from the default 32 -- same script. */
    wb_mode_config duos;
    wb_mode_config_defaults(&m, &duos);
    for (int i = 0; i < duos.nvalues; i++) {
        if (!strcmp(duos.values[i].name, "max_players"))
            snprintf(duos.values[i].value, sizeof duos.values[i].value, "2");
        if (!strcmp(duos.values[i].name, "round_seconds"))
            snprintf(duos.values[i].value, sizeof duos.values[i].value, "300");
    }
    CHECK(wb_mode_config_validate(&m, &duos, err, sizeof err) == 0,
          "the duos config validates");
    CHECK(wb_mode_config_get_int(&m, &duos, "max_players", 0) == 2,
          "duos is a different world from the default 32");
    CHECK(wb_mode_config_get_float(&m, &duos, "round_seconds", 0.0) == 300.0,
          "duos also changes the round length");

    /* ---- required keys --------------------------------------------------- */
    wb_mode_manifest req;
    const char *kReq =
        "{ \"name\": \"req\", \"entry\": \"m.lua\","
        "  \"config\": [ {\"name\": \"token\", \"type\": \"string\","
        "               \"default\": \"\", \"required\": true} ] }\n";
    write_file("build/test_mode_req.json", kReq);
    CHECK(wb_mode_manifest_load("build/test_mode_req.json", &req) == 0,
          "required-key manifest loads");
    wb_mode_config empty;
    memset(&empty, 0, sizeof empty);
    CHECK(wb_mode_config_validate(&req, &empty, err, sizeof err) != 0,
          "a missing required key is rejected");
    CHECK(strstr(err, "missing required") != NULL, "missing-required error is reported");

    /* ---- malformed manifests --------------------------------------------- */
    wb_mode_manifest bad;
    write_file("build/test_mode_bad.json", kBadTypeJson);
    CHECK(wb_mode_manifest_load("build/test_mode_bad.json", &bad) != 0,
          "an unknown config type fails the load");
    CHECK(bad.error[0] != 0, "the load error is reported");

    wb_mode_manifest inv;
    write_file("build/test_mode_inv.json", kInvertedJson);
    CHECK(wb_mode_manifest_load("build/test_mode_inv.json", &inv) == 0,
          "an inverted range still loads");
    CHECK(wb_mode_manifest_validate(&inv, err, sizeof err) != 0,
          "an inverted range fails validation");

    wb_mode_manifest none;
    CHECK(wb_mode_manifest_load("build/does_not_exist.json", &none) != 0,
          "a missing manifest fails");
    CHECK(strstr(none.error, "cannot read") != NULL, "missing-file error is reported");

    /* ---- validate a manifest with no entry -------------------------------- */
    wb_mode_manifest noent;
    const char *kNoEnt = "{ \"name\": \"noentry\" }\n";
    write_file("build/test_mode_noent.json", kNoEnt);
    CHECK(wb_mode_manifest_load("build/test_mode_noent.json", &noent) == 0,
          "a manifest with no entry loads");
    CHECK(wb_mode_manifest_validate(&noent, err, sizeof err) != 0,
          "a manifest with no entry does not validate");

    printf("\n=== MODE TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

/*
 * wb_cvar_test.c -- LP-40 gate: the console variable registry.
 *
 * Devil's-advocate cases: clamping to the range, a type mismatch being rejected,
 * the change callback firing only on a real change, reset returning to the
 * default, and the dirty flag.
 */
#include "wb_cvar.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static int g_changes = 0;
static void on_change(wb_cvar *c) { (void)c; g_changes++; }

int main(void) {
    wb_cvar_registry r;
    CHECK(wb_cvar_registry_init(&r, 8) == 0, "the registry is created");

    /* ---- registration ---- */
    wb_cvar *fps = wb_cvar_register(&r, "fps_max", WB_CVAR_INT);
    CHECK(fps != NULL, "an int cvar is registered");
    CHECK(wb_cvar_find(&r, "fps_max") == fps, "the cvar is found by name");
    CHECK(wb_cvar_find(&r, "nope") == NULL, "an unknown name is not found");
    CHECK(wb_cvar_register(&r, "fps_max", WB_CVAR_INT) == NULL, "a duplicate name is rejected");
    CHECK(wb_cvar_register(&r, NULL, WB_CVAR_INT) == NULL, "a NULL name is rejected");

    wb_cvar *vol = wb_cvar_register(&r, "volume", WB_CVAR_FLOAT);
    wb_cvar *vsync = wb_cvar_register(&r, "vsync", WB_CVAR_BOOL);
    wb_cvar *name = wb_cvar_register(&r, "player_name", WB_CVAR_STRING);
    CHECK(vol && vsync && name, "the other types register");
    CHECK(r.n == 4, "the registry holds four cvars");

    /* ---- defaults ---- */
    wb_cvar_set_default(fps, WB_CVAR_INT, 60, 0, 0, NULL);
    fps->lo = 30.0f; fps->hi = 240.0f;
    fps->on_change = on_change;
    CHECK(fps->value.i == 60, "the int default is applied");
    wb_cvar_set_default(vol, WB_CVAR_FLOAT, 0, 0.8f, 0, NULL);
    vol->lo = 0.0f; vol->hi = 1.0f;
    CHECK(vol->value.f == 0.8f, "the float default is applied");
    wb_cvar_set_default(vsync, WB_CVAR_BOOL, 0, 0, 1, NULL);
    CHECK(vsync->value.b == 1, "the bool default is applied");
    wb_cvar_set_default(name, WB_CVAR_STRING, 0, 0, 0, "player");
    CHECK(strcmp(name->value.s, "player") == 0, "the string default is applied");

    /* ---- clamping ---- */
    CHECK(wb_cvar_set_int(fps, 999) == 0, "setting the int succeeds");
    CHECK(fps->value.i == 240, "an over-range value clamps high");
    CHECK(wb_cvar_set_int(fps, 10) == 0, "setting the int again succeeds");
    CHECK(fps->value.i == 30, "an under-range value clamps low");
    CHECK(wb_cvar_set_float(vol, 5.0f) == 0 && vol->value.f == 1.0f, "a float clamps high");
    CHECK(wb_cvar_set_float(vol, -1.0f) == 0 && vol->value.f == 0.0f, "a float clamps low");

    /* ---- type mismatches are rejected ---- */
    CHECK(wb_cvar_set_float(fps, 1.0f) == -1, "a float setter on an int cvar is rejected");
    CHECK(wb_cvar_set_int(vol, 1) == -1, "an int setter on a float cvar is rejected");
    CHECK(wb_cvar_set_string(fps, "x") == -1, "a string setter on an int cvar is rejected");
    CHECK(wb_cvar_set_bool(name, 1) == -1, "a bool setter on a string cvar is rejected");
    CHECK(fps->value.i == 30, "a rejected assignment leaves the value alone");

    /* ---- the change callback fires only on a real change ---- */
    g_changes = 0;
    CHECK(wb_cvar_set_int(fps, 120) == 0, "setting to a new value succeeds");
    CHECK(g_changes == 1, "the callback fired once");
    CHECK(wb_cvar_set_int(fps, 120) == 0, "setting the same value succeeds");
    CHECK(g_changes == 1, "the callback did not fire for a no-op");
    CHECK(wb_cvar_set_int(fps, 999) == 0, "setting out of range succeeds");
    CHECK(fps->value.i == 240 && g_changes == 2, "the callback fired for the clamped change");
    CHECK(wb_cvar_set_int(fps, 500) == 0 && g_changes == 2, "a clamp to the same value is a no-op");

    /* ---- the dirty flag ---- */
    CHECK(wb_cvar_dirty(fps) == 1, "a changed cvar is dirty");
    wb_cvar_clear_dirty(fps);
    CHECK(wb_cvar_dirty(fps) == 0, "clearing the dirty flag works");
    CHECK(wb_cvar_dirty(NULL) == 0, "a NULL cvar is not dirty");

    /* ---- bool and string ---- */
    CHECK(wb_cvar_set_bool(vsync, 0) == 0 && vsync->value.b == 0, "the bool toggles off");
    CHECK(wb_cvar_set_bool(vsync, 42) == 0 && vsync->value.b == 1, "any nonzero is true");
    CHECK(wb_cvar_set_string(name, "waef") == 0, "the string is set");
    CHECK(strcmp(name->value.s, "waef") == 0, "the string value is stored");

    /* ---- reset returns to the default ---- */
    CHECK(wb_cvar_reset(fps) == 0 && fps->value.i == 60, "reset restores the int default");
    CHECK(wb_cvar_reset(vol) == 0 && vol->value.f == 0.8f, "reset restores the float default");
    CHECK(wb_cvar_reset(vsync) == 0 && vsync->value.b == 1, "reset restores the bool default");
    CHECK(wb_cvar_reset(name) == 0 && strcmp(name->value.s, "player") == 0, "reset restores the string default");

    /* ---- a full registry refuses to grow ---- */
    wb_cvar_registry small;
    wb_cvar_registry_init(&small, 1);
    CHECK(wb_cvar_register(&small, "a", WB_CVAR_INT) != NULL, "the first slot is taken");
    CHECK(wb_cvar_register(&small, "b", WB_CVAR_INT) == NULL, "a full registry refuses to grow");

    /* ---- determinism ---- */
    wb_cvar_registry a, b;
    wb_cvar_registry_init(&a, 8);
    wb_cvar_registry_init(&b, 8);
    wb_cvar *ai = wb_cvar_register(&a, "x", WB_CVAR_INT);
    wb_cvar *bi = wb_cvar_register(&b, "x", WB_CVAR_INT);
    wb_cvar_set_default(ai, WB_CVAR_INT, 5, 0, 0, NULL);
    wb_cvar_set_default(bi, WB_CVAR_INT, 5, 0, 0, NULL);
    ai->lo = bi->lo = 0.0f; ai->hi = bi->hi = 10.0f;
    int same = 1;
    for (int v = -5; v <= 15; v++) same &= (wb_cvar_set_int(ai, v) == wb_cvar_set_int(bi, v));
    same &= (ai->value.i == bi->value.i) && (ai->dirty == bi->dirty);
    CHECK(same, "the registry is deterministic");

    /* ---- degenerate input ---- */
    CHECK(wb_cvar_registry_init(NULL, 8) == -1, "a NULL registry is rejected");
    CHECK(wb_cvar_registry_init(&r, 0) == -1, "a zero capacity is rejected");
    CHECK(wb_cvar_set_int(NULL, 1) == -1, "a NULL cvar rejects an int set");
    CHECK(wb_cvar_reset(NULL) == -1, "a NULL cvar rejects a reset");
    CHECK(wb_cvar_set_string(name, NULL) == -1, "a NULL string is rejected");

    wb_cvar_registry_free(&r);
    wb_cvar_registry_free(&small);
    wb_cvar_registry_free(&a);
    wb_cvar_registry_free(&b);
    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

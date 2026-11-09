/*
 * wb_edit_test.c -- slice 17 gate: the in-world editor.
 *
 * Proves the edit document model: edits apply to the live world immediately,
 * undo/redo invert them exactly, and the document saves and replays into a
 * fresh world with the same result.
 */
#include "wb_edit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static int nearly(float a, float b, float tol) { return fabsf(a - b) <= tol; }

static wb_world *mk_world(void) {
    wb_world *w = (wb_world *)calloc(1, sizeof *w);
    wb_world_init(w);
    wb_register_comp(w, "position", sizeof(float) * 3);
    wb_register_comp(w, "velocity", sizeof(float) * 3);
    wb_register_comp(w, "mass",     sizeof(float));
    wb_register_comp(w, "label",    sizeof(char[32]));
    return w;
}

static int pos_t(void) { return 0; }

/* After an undo/redo an entity can be recycled into a new handle, so look up
 * whichever entity is actually live rather than assuming index 0. */
static wb_entity first_live(wb_world *w) {
    for (uint32_t i = 0; i < w->next_index; i++) {
        if (!w->alive[i]) continue;
        wb_entity e = { i, w->gen[i] };
        if (wb_entity_valid(w, e)) return e;
    }
    wb_entity none = { 0, 0 };
    return none;
}

static void op_place(wb_edit_op *op, float x, float y, float z, const char *label) {
    memset(op, 0, sizeof *op);
    op->kind = WB_EDIT_PLACE;
    op->a[0] = x; op->a[1] = y; op->a[2] = z;
    snprintf(op->label, sizeof op->label, "%s", label);
}

static void op_move(wb_edit_op *op, uint32_t entity, float x, float y, float z) {
    memset(op, 0, sizeof *op);
    op->kind = WB_EDIT_MOVE;
    op->entity = entity;
    op->a[0] = x; op->a[1] = y; op->a[2] = z;
}

static void op_set_mass(wb_edit_op *op, uint32_t entity, float m) {
    memset(op, 0, sizeof *op);
    op->kind = WB_EDIT_SET_COMP;
    op->entity = entity;
    op->comp = 2;            /* mass */
    op->a[0] = m;
}

int main(void) {
    wb_world *w = mk_world();
    wb_editor *ed = wb_editor_new(w);
    CHECK(ed != NULL, "editor created");
    CHECK(!wb_editor_can_undo(ed) && !wb_editor_can_redo(ed),
          "nothing to undo or redo at the start");

    /* ---- place ---------------------------------------------------- */
    wb_edit_op op;
    op_place(&op, 1.0f, 2.0f, 3.0f, "crate");
    CHECK(wb_editor_apply(ed, &op) == 0, "place applied");
    CHECK(wb_entity_count(w) == 1, "place created one entity");
    CHECK(wb_editor_count(ed) == 1, "edit list has one op");
    CHECK(wb_editor_can_undo(ed), "can undo after an edit");

    wb_entity e0 = { 0, 0 };
    float *p = (float *)wb_comp_get(w, e0, pos_t());
    CHECK(p && nearly(p[0], 1.0f, 1e-6) && nearly(p[1], 2.0f, 1e-6) &&
          nearly(p[2], 3.0f, 1e-6),
          "placed entity is at the requested position");
    char *lbl = (char *)wb_comp_get(w, e0, 3);
    CHECK(lbl && !strcmp(lbl, "crate"), "placed entity carries its label");

    /* ---- move ----------------------------------------------------- */
    op_move(&op, 0, 10.0f, 20.0f, 30.0f);
    CHECK(wb_editor_apply(ed, &op) == 0, "move applied");
    p = (float *)wb_comp_get(w, e0, pos_t());
    CHECK(nearly(p[0], 10.0f, 1e-6) && nearly(p[1], 20.0f, 1e-6),
          "moved entity is at the new position");

    /* ---- live edit is the same path -------------------------------- */
    op_set_mass(&op, 0, 7.5f);
    CHECK(wb_editor_live(ed, &op) == 0, "live edit applied");
    float *m = (float *)wb_comp_get(w, e0, 2);
    CHECK(m && nearly(*m, 7.5f, 1e-6), "live edit changed the component");

    /* ---- undo / redo ---------------------------------------------- */
    CHECK(wb_editor_undo(ed) == 0, "undo the mass edit");
    m = (float *)wb_comp_get(w, e0, 2);
    CHECK(m && nearly(*m, 1.0f, 1e-6), "undo restored the component value");

    CHECK(wb_editor_redo(ed) == 0, "redo the mass edit");
    m = (float *)wb_comp_get(w, e0, 2);
    CHECK(m && nearly(*m, 7.5f, 1e-6), "redo re-applied the component value");

    CHECK(wb_editor_undo(ed) == 0, "undo the mass edit again");
    CHECK(wb_editor_undo(ed) == 0, "undo the move");
    p = (float *)wb_comp_get(w, e0, pos_t());
    CHECK(nearly(p[0], 1.0f, 1e-6) && nearly(p[1], 2.0f, 1e-6),
          "undo restored the original position");

    CHECK(wb_editor_undo(ed) == 0, "undo the place");
    CHECK(wb_entity_count(w) == 0, "undoing the place removed the entity");
    CHECK(!wb_editor_can_undo(ed), "nothing left to undo");

    CHECK(wb_editor_redo(ed) == 0, "redo the place");
    CHECK(wb_entity_count(w) == 1, "redo re-created the entity");
    e0 = first_live(w);
    p = (float *)wb_comp_get(w, e0, pos_t());
    CHECK(p && nearly(p[0], 1.0f, 1e-6), "redone entity is at its placed position");

    /* Redo the remaining ops so the document is the full three-op history. */
    CHECK(wb_editor_redo(ed) == 0, "redo the move");
    CHECK(wb_editor_redo(ed) == 0, "redo the mass edit");
    CHECK(wb_editor_count(ed) == 3, "all three ops are current again");

    /* ---- save / replay -------------------------------------------- */
    CHECK(wb_editor_save(ed, "build/test_edits.json") == 0, "edit list saved");

    wb_world *w2 = mk_world();
    wb_editor *ed2 = wb_editor_new(w2);
    CHECK(wb_editor_replay(ed2, "build/test_edits.json") == 0, "edit list replayed");
    CHECK(wb_entity_count(w2) == 1, "replay produced the same entity count");
    wb_entity e2 = first_live(w2);
    p = (float *)wb_comp_get(w2, e2, pos_t());
    CHECK(p && nearly(p[0], 10.0f, 1e-6) && nearly(p[1], 20.0f, 1e-6) &&
          nearly(p[2], 30.0f, 1e-6),
          "replay reproduced the moved position");
    m = (float *)wb_comp_get(w2, e2, 2);
    CHECK(m && nearly(*m, 7.5f, 1e-6), "replay reproduced the component edit");

    /* ---- boundaries ------------------------------------------------ */
    CHECK(wb_editor_undo(ed2) == 0, "replayed editor can undo");
    CHECK(wb_editor_undo(ed2) == 0, "replayed editor can undo again");
    CHECK(wb_editor_undo(ed2) == 0, "replayed editor can undo a third time");
    CHECK(!wb_editor_can_undo(ed2), "replayed editor stops at the beginning");

    wb_editor_free(ed);
    wb_editor_free(ed2);
    wb_world_free(w);
    wb_world_free(w2);

    printf("\n=== EDIT TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

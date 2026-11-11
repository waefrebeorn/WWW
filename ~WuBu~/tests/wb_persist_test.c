/*
 * wb_persist_test.c -- LP-102 stone 1 gate: per-level persistence.
 *
 * Devil's-advocate cases: a derived level owns no bytes, so writing to it is
 * refused rather than silently stored; an ephemeral level is never durable, so a
 * stadium session cannot be persisted at all; a stadium cell's durable state
 * belongs to the patch BELOW it, because the session that sits on top of it ends;
 * a continent cell belongs to the world, because continent geometry is derived
 * from the seed and storing a copy would let it disagree with its source; the
 * seed is written once, so a second write is refused rather than changing the
 * world every lower level is derived from; a write through a level that does not
 * own the cell is refused, in both directions; and no refusal advances the epoch,
 * because the epoch is the fence a caller optimistically trusts.
 */
#include "wb_persist.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

/* The ladder from the pad: world seed -> continent geometry -> city props ->
 * patch terrain -> stadium session. */
#define L_WORLD    0
#define L_CONTINENT 1
#define L_CITY     2
#define L_PATCH    3
#define L_STADIUM  4

static const char *DEEP = "01230123012301230123012301230123012301";  /* depth 30 */

static void build_ladder(wb_persist_plan *p) {
    wb_persist_init(p);
    wb_persist_add(p, "world", 2, WB_PERSIST_SEED);
    wb_persist_add(p, "continent", 6, WB_PERSIST_DERIVED);
    wb_persist_add(p, "city", 12, WB_PERSIST_AUTHORED);
    wb_persist_add(p, "patch", 18, WB_PERSIST_PATCHED);
    wb_persist_add(p, "stadium", 24, WB_PERSIST_EPHEMERAL);
}

int main(void) {
    wb_persist_plan p;
    build_ladder(&p);

    /* ---- the ladder ---- */
    CHECK(wb_persist_count(&p) == 5, "the ladder has five levels");
    CHECK(wb_persist_depth(&p, L_WORLD) == 2, "the world sits at depth 2");
    CHECK(wb_persist_level_class(&p, L_CONTINENT) == WB_PERSIST_DERIVED,
          "continent geometry is derived");
    CHECK(wb_persist_level_class(&p, L_STADIUM) == WB_PERSIST_EPHEMERAL,
          "a stadium session is ephemeral");
    CHECK(strcmp(wb_persist_name(&p, L_CITY), "city") == 0, "a level keeps its name");
    CHECK(wb_persist_name(&p, 9) == NULL, "an out-of-range level has no name");
    CHECK(wb_persist_depth(&p, 9) == 0, "an out-of-range level has no depth");
    CHECK(wb_persist_written(NULL, 0) == 0, "a NULL plan has nothing written");
    CHECK(wb_persist_name(NULL, 0) == NULL, "a NULL plan has no level names");

    /* ---- the classes ---- */
    CHECK(wb_persist_class_durable(WB_PERSIST_SEED), "the seed owns durable bytes");
    CHECK(!wb_persist_class_durable(WB_PERSIST_DERIVED),
          "derived geometry owns no bytes");
    CHECK(wb_persist_class_durable(WB_PERSIST_AUTHORED), "props own durable bytes");
    CHECK(wb_persist_class_durable(WB_PERSIST_PATCHED), "terrain owns durable bytes");
    CHECK(!wb_persist_class_durable(WB_PERSIST_EPHEMERAL),
          "a session owns no durable bytes");
    CHECK(wb_persist_class_immutable(WB_PERSIST_SEED), "the seed is immutable");
    CHECK(!wb_persist_class_immutable(WB_PERSIST_AUTHORED), "props are mutable");
    CHECK(!wb_persist_class_immutable(WB_PERSIST_PATCHED), "terrain is mutable");
    CHECK(!wb_persist_class_immutable(WB_PERSIST_EPHEMERAL), "a session is not immutable");
    CHECK(strcmp(wb_persist_class_name(WB_PERSIST_DERIVED), "derived") == 0,
          "a class names itself");
    CHECK(strcmp(wb_persist_class_name((wb_persist_class)9), "?") == 0,
          "an unknown class has no name");

    /* ---- ownership: the deepest durable level at or above the cell ---- */
    CHECK(wb_persist_owner(&p, "0") == -1,
          "a cell above the coarsest level has no owner");
    CHECK(wb_persist_owner(&p, "") == -1, "the root has no owner");
    CHECK(wb_persist_owner(&p, "01") == L_WORLD, "a world cell belongs to the world");
    CHECK(wb_persist_owner(&p, "0123") == L_WORLD,
          "a cell between the world and the continent belongs to the world");
    CHECK(wb_persist_owner(&p, "012301") == L_WORLD,
          "a continent cell belongs to the world: geometry is derived and owns nothing");
    CHECK(wb_persist_owner(&p, "0123012301") == L_WORLD,
          "a cell above the city still belongs to the world");
    CHECK(wb_persist_owner(&p, "012301230123") == L_CITY, "a city cell belongs to the city");
    CHECK(wb_persist_owner(&p, "0123012301230123") == L_CITY,
          "a cell below the city still belongs to the city");
    CHECK(wb_persist_owner(&p, "012301230123012301") == L_PATCH,
          "a patch cell belongs to the patch");
    CHECK(wb_persist_owner(&p, "012301230123012301230123") == L_PATCH,
          "a stadium cell belongs to the patch: the session on top of it ends");
    CHECK(wb_persist_owner(&p, DEEP) == L_PATCH,
          "a cell below the finest level belongs to the finest durable one");
    CHECK(wb_persist_owner_for_depth(&p, 1) == -1, "depth 1 has no owner");
    CHECK(wb_persist_owner_for_depth(&p, 24) == L_PATCH, "depth 24 belongs to the patch");
    CHECK(wb_persist_owner(&p, NULL) == -1, "a NULL cell has no owner");
    CHECK(wb_persist_owner(NULL, "01") == -1, "a NULL plan has no owner");
    CHECK(wb_persist_owner(&p, "abc") == -1, "a malformed quadkey has no owner");

    /* ---- what a derived level derives from ---- */
    CHECK(wb_persist_source(&p, L_CONTINENT) == L_WORLD,
          "continent geometry derives from the world seed");
    CHECK(wb_persist_source(&p, L_CITY) == L_WORLD,
          "the city's nearest durable ancestor is the world");
    CHECK(wb_persist_source(&p, L_PATCH) == L_CITY,
          "the patch's nearest durable ancestor is the city");
    CHECK(wb_persist_source(&p, L_WORLD) == -1,
          "the seed derives from nothing");
    CHECK(wb_persist_source(&p, 9) == -1, "an out-of-range level has no source");
    CHECK(wb_persist_source(NULL, L_CITY) == -1, "a NULL plan has no source");

    /* ---- writing ---- */
    CHECK(wb_persist_epoch(&p) == 1, "a fresh plan starts at epoch 1");
    uint64_t e = 0;
    CHECK(wb_persist_write(&p, "012301230123", L_CITY, &e) == 0,
          "the owner accepts a durable write");
    CHECK(e == 2, "the write returns the plan epoch");
    CHECK(wb_persist_epoch(&p) == 2, "and the epoch advances");
    CHECK(wb_persist_written(&p, L_CITY) == 1, "the city is marked written");

    CHECK(wb_persist_write(&p, "012301230123", L_WORLD, &e) == -1,
          "a durable level that does not own the cell refuses the write");
    CHECK(e == 0, "a refused write returns no epoch");
    CHECK(wb_persist_epoch(&p) == 2, "and the epoch does not move");
    CHECK(wb_persist_write(&p, "012301230123012301", L_CITY, &e) == -1,
          "a finer level cannot take an ancestor's state");
    CHECK(wb_persist_write(&p, "012301", L_CONTINENT, &e) == -1,
          "a derived level refuses a write: it owns no bytes");
    CHECK(wb_persist_write(&p, "012301230123012301230123", L_STADIUM, &e) == -1,
          "an ephemeral level refuses a write: a session is never stored");
    CHECK(wb_persist_write(&p, "0", L_WORLD, &e) == -1,
          "a cell with no owner accepts no write");
    CHECK(wb_persist_write(&p, "012301230123", 9, &e) == -1,
          "an out-of-range level refuses a write");
    CHECK(wb_persist_write(&p, "abc", L_CITY, &e) == -1,
          "a malformed cell refuses a write");
    CHECK(wb_persist_write(NULL, "012301230123", L_CITY, &e) == -1,
          "a NULL plan refuses a write");
    CHECK(wb_persist_epoch(&p) == 2, "no refusal advanced the epoch");

    /* ---- the seed is written once ---- */
    CHECK(wb_persist_write(&p, "01", L_WORLD, &e) == 0, "the seed is written once");
    CHECK(e == 3, "and the epoch advances");
    CHECK(wb_persist_write(&p, "01", L_WORLD, &e) == -1,
          "the seed refuses a second write");
    CHECK(wb_persist_write(&p, "0123", L_WORLD, &e) == -1,
          "and it refuses a second write for any cell it owns");
    CHECK(wb_persist_epoch(&p) == 3, "the refused re-write did not advance the epoch");

    /* ---- adding levels ---- */
    wb_persist_plan q;
    wb_persist_init(&q);
    CHECK(wb_persist_add(&q, "world", 2, WB_PERSIST_SEED) == 0, "a seed is added");
    CHECK(wb_persist_add(&q, "continent", 2, WB_PERSIST_DERIVED) == -1,
          "a level at the same depth is refused");
    CHECK(wb_persist_add(&q, "continent", 1, WB_PERSIST_DERIVED) == -1,
          "a level shallower than the previous one is refused");
    CHECK(wb_persist_add(&q, "continent", 6, WB_PERSIST_DERIVED) == 0,
          "a deeper derived level is added");
    CHECK(wb_persist_add(&q, "city", 12, (wb_persist_class)9) == -1,
          "an unknown class is refused");
    CHECK(wb_persist_add(&q, "city", 12, WB_PERSIST_AUTHORED) == 0,
          "an authored level is added");
    CHECK(wb_persist_add(&q, "this-name-is-far-too-long-for-the-plan", 18,
                         WB_PERSIST_PATCHED) == -1,
          "an over-long name is refused");
    CHECK(wb_persist_add(&q, NULL, 18, WB_PERSIST_PATCHED) == -1,
          "a NULL name is refused");
    CHECK(wb_persist_add(NULL, "patch", 18, WB_PERSIST_PATCHED) == -1,
          "a NULL plan refuses a level");
    CHECK(wb_persist_count(&q) == 3, "the refused adds changed nothing");

    /* A derived level with no coarser durable level has nothing to derive from. */
    wb_persist_plan d;
    wb_persist_init(&d);
    CHECK(wb_persist_add(&d, "continent", 6, WB_PERSIST_DERIVED) == -1,
          "a first level cannot be derived");
    CHECK(wb_persist_add(&d, "stadium", 6, WB_PERSIST_EPHEMERAL) == 0,
          "an ephemeral first level is allowed");
    CHECK(wb_persist_add(&d, "continent", 12, WB_PERSIST_DERIVED) == -1,
          "a derived level still has no durable level to derive from");
    CHECK(wb_persist_count(&d) == 1, "the refused derivation changed nothing");

    /* ---- the ceiling ---- */
    wb_persist_plan full;
    wb_persist_init(&full);
    for (uint32_t i = 0; i < WB_PERSIST_MAX; i++) {
        char name[8];
        snprintf(name, sizeof name, "L%u", i);
        uint32_t cls = (i == 0) ? WB_PERSIST_SEED
                                 : (wb_persist_class)(1 + (i % 4));
        if (i > 0 && cls == WB_PERSIST_DERIVED) cls = WB_PERSIST_AUTHORED;
        CHECK(wb_persist_add(&full, name, i + 1, (wb_persist_class)cls) == 0,
              "a level is added up to the ceiling");
    }
    CHECK(wb_persist_add(&full, "one-too-many", WB_PERSIST_MAX + 1,
                         WB_PERSIST_PATCHED) == -1,
          "the ceiling refuses another level");
    CHECK(wb_persist_count(&full) == WB_PERSIST_MAX, "the plan is full");

    /* ---- an empty plan owns nothing ---- */
    wb_persist_plan empty;
    wb_persist_init(&empty);
    CHECK(wb_persist_count(&empty) == 0, "an empty plan has no levels");
    CHECK(wb_persist_owner(&empty, "012301230123") == -1,
          "an empty plan owns no cell");
    CHECK(wb_persist_write(&empty, "012301230123", 0, &e) == -1,
          "an empty plan accepts no write");
    CHECK(wb_persist_class_durable((wb_persist_class)99) == 0,
          "an unknown class owns no bytes");
    CHECK(wb_persist_class_immutable((wb_persist_class)99) == 0,
          "an unknown class is not immutable");
    wb_persist_init(NULL);

    /* ---- determinism: the same ladder agrees with itself ---- */
    wb_persist_plan r;
    build_ladder(&r);
    int same = 1;
    const char *cells[] = {"", "0", "01", "0123", "012301", "0123012301",
                           "012301230123", "0123012301230123",
                           "012301230123012301", "012301230123012301230123", DEEP};
    for (size_t i = 0; i < sizeof cells / sizeof cells[0]; i++)
        if (wb_persist_owner(&p, cells[i]) != wb_persist_owner(&r, cells[i]))
            same = 0;
    CHECK(same, "two ladders built the same way agree on every cell");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

/*
 * wb_place_test.c -- LP-105 stone 1 gate: instance placement at a level.
 *
 * Devil's-advocate cases: a request naming a place never falls back to a
 * general-purpose node (a typo'd address must refuse, not misplace); ties break to
 * the LOWEST node id so two servers agree without consulting; a dedicated node
 * holds one instance whatever its capacity says; a dedicated REQUEST needs an
 * empty node, which is a different test from a node being full; and re-placing an
 * instance id that is already placed consumes no second slot.
 */
#include "wb_place.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

enum { NODE_ANY1 = 1, NODE_ANY2 = 2, NODE_P100 = 3, NODE_P100_DED = 4, NODE_P500 = 5 };
enum { PLACE_100 = 100, PLACE_500 = 500 };

static int build(wb_place_pool *p) {
    if (wb_place_init(p, 8) != 0) return -1;
    if (wb_place_add(p, NODE_ANY1, WB_PLACE_ANY, 2, 0) != 0) return -1;
    if (wb_place_add(p, NODE_ANY2, WB_PLACE_ANY, 2, 0) != 0) return -1;
    if (wb_place_add(p, NODE_P100, PLACE_100, 4, 0) != 0) return -1;
    if (wb_place_add(p, NODE_P100_DED, PLACE_100, 1, WB_PLACE_DEDICATED) != 0) return -1;
    if (wb_place_add(p, NODE_P500, PLACE_500, 1, 0) != 0) return -1;
    return 0;
}

/* A compound literal is an lvalue, so &REQ(...) is legal -- unlike &REQ(...). */
#define REQ(id, address, flags) ((wb_place_request){ (id), (address), (flags) })

int main(void) {
    wb_place_pool p;
    CHECK(build(&p) == 0, "the fixture is built");

    /* ---- add refuses bad input ---- */
    CHECK(wb_place_add(&p, NODE_ANY1, WB_PLACE_ANY, 1, 0) == -1, "a duplicate node id is refused");
    CHECK(wb_place_add(&p, 99, WB_PLACE_ANY, 0, 0) == -1, "a zero capacity is refused");
    CHECK(wb_place_add(&p, 99, WB_PLACE_ANY, WB_PLACE_MAX_PER_NODE + 1, 0) == -1,
          "a capacity above the per-node limit is refused");
    {
        wb_place_pool lim;
        CHECK(wb_place_init(&lim, 2) == 0, "a small pool is created");
        CHECK(wb_place_add(&lim, 1, WB_PLACE_ANY, WB_PLACE_MAX_PER_NODE, 0) == 0,
              "a capacity at the limit is allowed");
        CHECK(wb_place_add(&lim, 2, WB_PLACE_ANY, 1, 0) == 0, "a second node is added");
        wb_place_free(&lim);
    }
    {
        wb_place_pool one;
        CHECK(wb_place_init(&one, 1) == 0, "a one-node pool is created");
        CHECK(wb_place_add(&one, 1, WB_PLACE_ANY, 1, 0) == 0, "the only slot is used");
        CHECK(wb_place_add(&one, 2, WB_PLACE_ANY, 1, 0) == -1, "a full pool refuses an add");
        wb_place_free(&one);
    }

    /* ---- shared placement fills the lowest node id first ---- */
    wb_place_result r = wb_place_alloc(&p, &REQ(1001, WB_PLACE_ANY, 0));
    CHECK(r.reason == WB_PLACE_OK && r.node == NODE_ANY1, "the first instance goes to the lowest node");
    CHECK(wb_place_load(&p, NODE_ANY1) == 1, "the node's load is one");
    r = wb_place_alloc(&p, &REQ(1002, WB_PLACE_ANY, 0));
    CHECK(r.reason == WB_PLACE_OK && r.node == NODE_ANY1, "a second instance shares the node");
    CHECK(wb_place_load(&p, NODE_ANY1) == 2, "the node is now full");
    r = wb_place_alloc(&p, &REQ(1003, WB_PLACE_ANY, 0));
    CHECK(r.reason == WB_PLACE_OK && r.node == NODE_ANY2, "the next instance goes to the next node");
    r = wb_place_alloc(&p, &REQ(1004, WB_PLACE_ANY, 0));
    CHECK(r.reason == WB_PLACE_OK && r.node == NODE_ANY2, "the second node fills");
    r = wb_place_alloc(&p, &REQ(1005, WB_PLACE_ANY, 0));
    CHECK(r.reason == WB_PLACE_REFUSED_FULL && r.node == 0, "a full pool refuses with FULL");

    /* ---- a named place goes to a node bound to it ---- */
    r = wb_place_alloc(&p, &REQ(2001, PLACE_100, 0));
    CHECK(r.reason == WB_PLACE_OK && r.node == NODE_P100, "a named place goes to its own node");
    CHECK(wb_place_load(&p, NODE_P100) == 1, "the node's load is one");
    CHECK(wb_place_load(&p, NODE_ANY1) == 2, "and the general-purpose nodes are untouched");
    r = wb_place_alloc(&p, &REQ(2002, PLACE_500, 0));
    CHECK(r.reason == WB_PLACE_OK && r.node == NODE_P500, "another named place goes to its node");

    /* ---- a typo'd address refuses rather than misplacing ---- */
    r = wb_place_alloc(&p, &REQ(2003, 999, 0));
    CHECK(r.reason == WB_PLACE_REFUSED_NO_NODE && r.node == 0,
          "an address no node serves is refused, not fallen back");
    r = wb_place_alloc(&p, &REQ(2004, PLACE_100, 0));
    CHECK(r.reason == WB_PLACE_OK && r.node == NODE_P100, "a named request skips general-purpose nodes");
    CHECK(wb_place_load(&p, NODE_P100) == 2, "the load is two");

    /* ---- dedicated nodes hold one instance whatever the capacity ---- */
    CHECK(wb_place_slots(&p, NODE_P100_DED) == 1, "a dedicated node exposes one slot");
    CHECK(wb_place_slots(&p, NODE_P100) == 4, "a shared node exposes its capacity");
    CHECK(wb_place_slots(&p, 999) == 0, "an unknown node exposes no slots");

    /* ---- a dedicated REQUEST needs an empty node ---- */
    r = wb_place_alloc(&p, &REQ(3001, PLACE_100, WB_PLACE_DEDICATED));
    CHECK(r.reason == WB_PLACE_OK && r.node == NODE_P100_DED,
          "a dedicated request takes the empty dedicated node");
    CHECK(wb_place_load(&p, NODE_P100_DED) == 1, "the dedicated node is now occupied");
    r = wb_place_alloc(&p, &REQ(3002, PLACE_100, WB_PLACE_DEDICATED));
    CHECK(r.reason == WB_PLACE_REFUSED_EXCLUSIVE && r.node == 0,
          "a second dedicated request is refused: no empty node serves that place");
    r = wb_place_alloc(&p, &REQ(3003, WB_PLACE_ANY, WB_PLACE_DEDICATED));
    CHECK(r.reason == WB_PLACE_REFUSED_EXCLUSIVE && r.node == 0,
          "a dedicated request with nowhere to go is refused EXCLUSIVE, not FULL");

    /* ---- re-placing the same instance id consumes no second slot ---- */
    uint64_t load_before = wb_place_load(&p, NODE_P100);
    r = wb_place_alloc(&p, &REQ(2001, PLACE_100, 0));
    CHECK(r.reason == WB_PLACE_REFUSED_DUPLICATE && r.node == NODE_P100,
          "re-placing a known instance id reports DUPLICATE");
    CHECK(wb_place_load(&p, NODE_P100) == load_before, "and consumes no second slot");
    CHECK(wb_place_contains(&p, NODE_P100, 2001) == 1, "the instance is on the node");
    CHECK(wb_place_contains(&p, NODE_P100, 9999) == 0, "an unknown instance is not");
    CHECK(wb_place_contains(&p, 999, 2001) == 0, "an unknown node holds nothing");

    /* ---- release ---- */
    CHECK(wb_place_release(&p, NODE_P100, 9999) == -1, "releasing an unknown instance is refused");
    CHECK(wb_place_release(&p, 999, 2001) == -1, "releasing from an unknown node is refused");
    CHECK(wb_place_release(&p, NODE_P100, 2001) == 0, "the holder releases");
    CHECK(wb_place_contains(&p, NODE_P100, 2001) == 0, "and the instance is gone");
    CHECK(wb_place_load(&p, NODE_P100) == load_before - 1, "and the load dropped by one");
    CHECK(wb_place_release(&p, NODE_P100, 2001) == -1, "a second release is refused");
    /* the freed slot is reusable, and the node keeps its order */
    r = wb_place_alloc(&p, &REQ(2005, PLACE_100, 0));
    CHECK(r.reason == WB_PLACE_OK && r.node == NODE_P100, "a freed slot is reusable");

    /* ---- clear ---- */
    wb_place_clear(&p);
    CHECK(wb_place_load(&p, NODE_P100) == 0, "clear empties every node");
    r = wb_place_alloc(&p, &REQ(4001, PLACE_100, 0));
    CHECK(r.reason == WB_PLACE_REFUSED_NO_NODE && r.node == 0, "a cleared pool places nothing");

    /* ---- determinism ---- */
    wb_place_pool x, y;
    CHECK(build(&x) == 0 && build(&y) == 0, "two pools are built");
    wb_place_request script[] = {
        REQ(1, WB_PLACE_ANY, 0), REQ(2, WB_PLACE_ANY, 0), REQ(3, PLACE_100, 0),
        REQ(4, PLACE_100, WB_PLACE_DEDICATED), REQ(5, WB_PLACE_ANY, 0), REQ(6, PLACE_500, 0),
    };
    int same = 1;
    for (size_t i = 0; i < sizeof script / sizeof script[0]; i++) {
        wb_place_result rx = wb_place_alloc(&x, &script[i]);
        wb_place_result ry = wb_place_alloc(&y, &script[i]);
        same &= (rx.reason == ry.reason && rx.node == ry.node);
    }
    CHECK(same, "two identical pools place identically");
    same = 1;
    for (uint64_t n = 1; n <= NODE_P500; n++)
        same &= (wb_place_load(&x, n) == wb_place_load(&y, n));
    CHECK(same, "and end with identical loads");

    /* ---- degenerate input ---- */
    CHECK(wb_place_init(NULL, 4) == -1, "a NULL pool is rejected");
    CHECK(wb_place_init(&x, 0) == -1, "a zero capacity is rejected");
    CHECK(wb_place_add(NULL, 1, 0, 1, 0) == -1, "a NULL pool refuses an add");
    r = wb_place_alloc(NULL, &script[0]);
    CHECK(r.node == 0 && r.reason == WB_PLACE_REFUSED_NO_NODE, "a NULL pool refuses to place");
    r = wb_place_alloc(&x, NULL);
    CHECK(r.node == 0 && r.reason == WB_PLACE_REFUSED_NO_NODE, "a NULL request is refused");
    r = wb_place_alloc(&x, &REQ(0, WB_PLACE_ANY, 0));
    CHECK(r.node == 0, "instance id 0 is refused");
    CHECK(wb_place_release(NULL, 1, 1) == -1, "a NULL pool refuses a release");
    CHECK(wb_place_load(NULL, 1) == 0, "a NULL pool has no load");
    CHECK(wb_place_contains(NULL, 1, 1) == 0, "a NULL pool contains nothing");

    wb_place_free(&p);
    wb_place_free(&x);
    wb_place_free(&y);
    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

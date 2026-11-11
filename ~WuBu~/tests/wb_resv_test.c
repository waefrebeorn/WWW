/*
 * wb_resv_test.c -- LP-107 stone 1 gate: the nested-volume reservation ledger.
 *
 * Devil's-advocate cases: a delegable ancestor permits a held descendant while a
 * non-delegable one refuses it; the reverse direction (holding an ancestor while
 * a descendant is held) is refused unless the ancestor delegated; a retry by the
 * SAME holder is harmless while a retry by anyone else is a conflict; a release
 * by a non-holder changes nothing; and a refused operation must not bump the
 * epoch, because the epoch is what a distributed caller fences on.
 */
#include "wb_resv.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

/* earth > asia > china, earth > europe, and moon > crater (moon delegable). */
enum { EARTH = 1, ASIA = 2, EUROPE = 3, CHINA = 4, MOON = 5, CRATER = 6 };
#define A 100
#define B 200

static int build(wb_resv_ledger *l) {
    if (wb_resv_init(l, 16) != 0) return -1;
    if (wb_resv_add(l, EARTH, 0, 0) != 0) return -1;
    if (wb_resv_add(l, ASIA, EARTH, 0) != 0) return -1;
    if (wb_resv_add(l, EUROPE, EARTH, 0) != 0) return -1;
    if (wb_resv_add(l, CHINA, ASIA, 0) != 0) return -1;
    if (wb_resv_add(l, MOON, 0, WB_RESV_DELEGABLE) != 0) return -1;
    if (wb_resv_add(l, CRATER, MOON, 0) != 0) return -1;
    return 0;
}

int main(void) {
    wb_resv_ledger l;
    CHECK(build(&l) == 0, "the fixture is built");
    CHECK(wb_resv_count(&l) == 6, "six volumes are registered");

    /* ---- add refuses bad input ---- */
    CHECK(wb_resv_add(&l, EARTH, 0, 0) == -1, "a duplicate volume is refused");
    CHECK(wb_resv_add(&l, 99, 12345, 0) == -1, "an unknown parent is refused");
    CHECK(wb_resv_add(&l, 0, 0, 0) == -1, "volume 0 is refused (it means 'no parent')");
    CHECK(wb_resv_add(&l, ASIA, EARTH, 0) == -1, "a re-added volume is refused");
    CHECK(wb_resv_count(&l) == 6, "refused adds register nothing");

    /* ---- a cycle is refused ---- */
    wb_resv_ledger c;
    CHECK(wb_resv_init(&c, 8) == 0, "a cycle ledger is created");
    CHECK(wb_resv_add(&c, 1, 0, 0) == 0, "node 1 is added");
    CHECK(wb_resv_add(&c, 2, 1, 0) == 0, "node 2 is added under 1");
    CHECK(wb_resv_add(&c, 1, 2, 0) == -1, "moving a node under its own child is refused");

    /* ---- exclusivity ---- */
    CHECK(wb_resv_reserve(&l, EARTH, A) == 0, "earth is reserved");
    CHECK(wb_resv_is_held(&l, EARTH) == 1, "earth is held");
    CHECK(wb_resv_holder(&l, EARTH) == A, "the holder is recorded");
    CHECK(wb_resv_reserve(&l, ASIA, B) == -1, "a child of a held non-delegable volume is refused");
    CHECK(wb_resv_reserve(&l, EUROPE, B) == -1, "the other child is refused too");
    CHECK(wb_resv_reserve(&l, CHINA, B) == -1, "a grandchild is refused");
    CHECK(wb_resv_holder(&l, ASIA) == 0, "the refusal left asia free");
    CHECK(wb_resv_reserve(&l, ASIA, A) == 0, "the same holder may hold a whole chain");
    CHECK(wb_resv_reserve(&l, EARTH, A) == 0, "a retry by the same holder is harmless");
    CHECK(wb_resv_reserve(&l, EARTH, B) == -1, "a retry by another holder is a conflict");
    CHECK(wb_resv_reserve(&l, EARTH, 0) == -1, "holder 0 is refused (it means 'no holder')");
    CHECK(wb_resv_reserve(&l, 999, A) == -1, "an unknown volume is refused");

    /* ---- release ---- */
    CHECK(wb_resv_release(&l, ASIA, B) == -1, "a non-holder cannot release");
    CHECK(wb_resv_holder(&l, ASIA) == A, "the failed release changed nothing");
    CHECK(wb_resv_release(&l, EUROPE, A) == -1, "a free volume cannot be released");
    CHECK(wb_resv_release(&l, ASIA, A) == 0, "the holder releases");
    CHECK(wb_resv_holder(&l, ASIA) == 0, "the release took effect");
    CHECK(wb_resv_release(&l, ASIA, A) == -1, "a second release is refused");
    /* asia is still under earth's exclusive hold, so freeing it does not open it to
     * everyone -- only the holder of the covering volume may retake it */
    CHECK(wb_resv_reserve(&l, ASIA, B) == -1, "a freed child of a held volume stays blocked for others");
    CHECK(wb_resv_reserve(&l, ASIA, A) == 0, "the same holder may retake a freed volume");

    /* ---- delegation ---- */
    CHECK(wb_resv_is_delegable(&l, MOON) == 1, "moon is delegable");
    CHECK(wb_resv_is_delegable(&l, EARTH) == 0, "earth is not delegable");
    CHECK(wb_resv_reserve(&l, MOON, A) == 0, "moon is reserved");
    CHECK(wb_resv_reserve(&l, CRATER, B) == 0, "a child of a delegable volume may be held by another holder");
    CHECK(wb_resv_reserve(&l, EARTH, B) == -1, "an ancestor of a held volume is refused when it did not delegate");
    CHECK(wb_resv_holder(&l, EARTH) == A, "the refused ancestor is unchanged");
    CHECK(wb_resv_release(&l, CRATER, B) == 0, "the crater holder releases");
    CHECK(wb_resv_reserve(&l, CRATER, A) == 0,
          "a freed volume under a delegable ancestor can be taken by anyone");

    /* ---- ancestry and coverage ---- */
    CHECK(wb_resv_is_ancestor(&l, CHINA, EARTH) == 1, "earth is an ancestor of china");
    CHECK(wb_resv_is_ancestor(&l, CHINA, ASIA) == 1, "asia is an ancestor of china");
    CHECK(wb_resv_is_ancestor(&l, CHINA, CHINA) == 1, "a volume is its own ancestor");
    CHECK(wb_resv_is_ancestor(&l, EARTH, CHINA) == 0, "a descendant is not an ancestor");
    CHECK(wb_resv_is_ancestor(&l, CHINA, MOON) == 0, "an unrelated volume is not an ancestor");
    CHECK(wb_resv_is_ancestor(&l, 999, EARTH) == 0, "an unknown volume has no ancestry");
    CHECK(wb_resv_covers(&l, CHINA, A) == 1, "a holder's reservation covers its descendants");
    CHECK(wb_resv_covers(&l, CHINA, B) == 0, "another holder's reservation does not");
    CHECK(wb_resv_covers(&l, CRATER, A) == 1, "a delegable ancestor still covers its descendants");
    CHECK(wb_resv_covers(&l, CRATER, B) == 0, "a released holder no longer covers the volume");
    CHECK(wb_resv_covers(&l, EARTH, 0) == 0, "holder 0 covers nothing");
    CHECK(wb_resv_covers(&l, 999, A) == 0, "an unknown volume is covered by no one");

    /* ---- the epoch fences ---- */
    uint64_t e0 = wb_resv_epoch(&l);
    CHECK(wb_resv_reserve(&l, EUROPE, B) == -1, "a conflicting reserve is refused");
    CHECK(wb_resv_epoch(&l) == e0, "a refused reserve does not bump the epoch");
    CHECK(wb_resv_release(&l, EUROPE, B) == -1, "a refused release");
    CHECK(wb_resv_epoch(&l) == e0, "a refused release does not bump the epoch");
    CHECK(wb_resv_add(&l, 77, 0, 0) == 0, "an add succeeds");
    CHECK(wb_resv_epoch(&l) == e0 + 1, "an add bumps the epoch");
    uint64_t e1 = wb_resv_epoch(&l);
    CHECK(wb_resv_release(&l, MOON, A) == 0, "a release succeeds");
    CHECK(wb_resv_epoch(&l) == e1 + 1, "a release bumps the epoch");

    /* ---- clear ---- */
    wb_resv_clear(&l);
    CHECK(wb_resv_count(&l) == 0, "clear empties the ledger");
    CHECK(wb_resv_holder(&l, EARTH) == 0, "a cleared ledger holds nothing");
    CHECK(wb_resv_epoch(&l) == 1, "clear resets the epoch");

    /* ---- determinism ---- */
    wb_resv_ledger x, y;
    CHECK(build(&x) == 0 && build(&y) == 0, "two identical ledgers are built");
    const uint64_t script[] = { EARTH, ASIA, EUROPE, MOON, CRATER };
    const uint64_t holders[] = { A, B, A, B, A };
    for (int i = 0; i < 5; i++) {
        wb_resv_reserve(&x, script[i], holders[i]);
        wb_resv_reserve(&y, script[i], holders[i]);
    }
    CHECK(wb_resv_epoch(&x) == wb_resv_epoch(&y), "the epoch is deterministic");
    int same = 1;
    for (int i = 0; i < 5; i++)
        same &= (wb_resv_holder(&x, script[i]) == wb_resv_holder(&y, script[i]));
    CHECK(same, "the holders are deterministic");
    /* the same volumes registered in a different (but still parents-first)
     * order: the result must not change */
    wb_resv_ledger z;
    wb_resv_init(&z, 16);
    CHECK(wb_resv_add(&z, MOON, 0, WB_RESV_DELEGABLE) == 0, "moon is registered first");
    CHECK(wb_resv_add(&z, CRATER, MOON, 0) == 0, "crater under it");
    CHECK(wb_resv_add(&z, EARTH, 0, 0) == 0, "earth is registered late");
    CHECK(wb_resv_add(&z, EUROPE, EARTH, 0) == 0, "europe under it");
    CHECK(wb_resv_add(&z, ASIA, EARTH, 0) == 0, "asia under it");
    CHECK(wb_resv_add(&z, CHINA, ASIA, 0) == 0, "china under it");
    CHECK(wb_resv_reserve(&z, EARTH, A) == 0, "the reordered ledger behaves the same");
    CHECK(wb_resv_reserve(&z, ASIA, B) == -1, "exclusion holds regardless of registration order");
    CHECK(wb_resv_reserve(&z, CRATER, B) == 0, "delegation holds regardless of registration order");

    /* ---- degenerate input ---- */
    CHECK(wb_resv_init(NULL, 4) == -1, "a NULL ledger is rejected");
    CHECK(wb_resv_init(&x, 0) == -1, "a zero capacity is rejected");
    CHECK(wb_resv_add(NULL, 1, 0, 0) == -1, "a NULL ledger refuses an add");
    CHECK(wb_resv_reserve(NULL, 1, A) == -1, "a NULL ledger refuses a reserve");
    CHECK(wb_resv_release(NULL, 1, A) == -1, "a NULL ledger refuses a release");
    CHECK(wb_resv_holder(NULL, 1) == 0, "a NULL ledger has no holders");
    CHECK(wb_resv_is_held(NULL, 1) == 0, "a NULL ledger holds nothing");
    CHECK(wb_resv_is_delegable(NULL, 1) == 0, "a NULL ledger has no delegable volumes");
    CHECK(wb_resv_is_ancestor(NULL, 1, 1) == 0, "a NULL ledger has no ancestry");
    CHECK(wb_resv_covers(NULL, 1, A) == 0, "a NULL ledger covers nothing");
    CHECK(wb_resv_epoch(NULL) == 0, "a NULL ledger has no epoch");
    CHECK(wb_resv_count(NULL) == 0, "a NULL ledger has no volumes");
    {
        wb_resv_ledger one;
        wb_resv_init(&one, 1);
        CHECK(wb_resv_add(&one, 1, 0, 0) == 0, "the only slot is used");
        CHECK(wb_resv_add(&one, 2, 1, 0) == -1, "a full ledger refuses an add");
    }

    wb_resv_free(&l);
    wb_resv_free(&c);
    wb_resv_free(&x);
    wb_resv_free(&y);
    wb_resv_free(&z);
    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

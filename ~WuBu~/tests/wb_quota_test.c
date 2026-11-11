/*
 * wb_quota_test.c -- LP-113 stone 1 gate: per-volume capacity and quotas.
 *
 * Devil's-advocate cases: a reservation spends the capacity of every ANCESTOR,
 * so a full planet refuses a stadium even when the stadium itself is empty, and a
 * full stadium refuses a reservation even when the planet has room; the capacity a
 * volume can offer is the bottleneck along its chain, not its own number; a quota
 * covers a holder's whole SUBTREE, so a sibling volume is outside it while a
 * descendant is inside it; a quota cannot be set below what is already held; and
 * no refusal -- of a reservation, a release or a quota -- advances the epoch.
 */
#include "wb_quota.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

/* earth > asia > china, earth > europe, and moon > crater. */
enum { EARTH = 1, ASIA = 2, EUROPE = 3, CHINA = 4, MOON = 5, CRATER = 6 };
#define A 100
#define B 200
#define C 300

static int build(wb_quota_ledger *l) {
    if (wb_quota_init(l, 16, 32) != 0) return -1;
    if (wb_quota_add(l, EARTH, 0, 10) != 0) return -1;
    if (wb_quota_add(l, ASIA, EARTH, 6) != 0) return -1;
    if (wb_quota_add(l, EUROPE, EARTH, 4) != 0) return -1;
    if (wb_quota_add(l, CHINA, ASIA, 3) != 0) return -1;
    if (wb_quota_add(l, MOON, 0, 2) != 0) return -1;
    if (wb_quota_add(l, CRATER, MOON, 2) != 0) return -1;
    return 0;
}

int main(void) {
    wb_quota_ledger l;
    CHECK(build(&l) == 0, "the fixture is built");
    CHECK(wb_quota_count(&l) == 6, "six volumes are registered");
    CHECK(wb_quota_epoch(&l) == 1, "a fresh ledger starts at epoch 1");

    /* ---- add refuses bad input ---- */
    CHECK(wb_quota_add(&l, EARTH, 0, 5) == -1, "a duplicate volume is refused");
    CHECK(wb_quota_add(&l, 0, 0, 5) == -1, "volume 0 is refused (it means unknown)");
    CHECK(wb_quota_add(&l, 7, 99, 5) == -1, "an unknown parent is refused");
    CHECK(wb_quota_add(&l, 7, 7, 5) == -1, "a volume cannot be its own parent");
    CHECK(wb_quota_add(&l, 7, 0, 0) == 0, "a zero capacity is allowed (it holds nothing)");
    CHECK(wb_quota_capacity(&l, 7) == 0, "and it is really zero");
    CHECK(wb_quota_add(NULL, 8, 0, 5) == -1, "a NULL ledger refuses a volume");
    CHECK(wb_quota_count(&l) == 7, "the refused adds changed nothing");

    /* ---- capacity is spent along the ancestor chain ---- */
    CHECK(wb_quota_capacity(&l, EARTH) == 10, "earth holds ten");
    CHECK(wb_quota_used(&l, EARTH) == 0, "and starts empty");
    CHECK(wb_quota_room(&l, EARTH) == 10, "with ten free");
    CHECK(wb_quota_available(&l, EARTH) == 10, "a root offers its own free capacity");
    CHECK(wb_quota_available(&l, CHINA) == 3,
          "a deep volume offers the bottleneck along its chain");
    CHECK(wb_quota_available(&l, CRATER) == 2, "the moon chain is independent");

    CHECK(wb_quota_reserve(&l, EARTH, A, 10) == 0, "earth is filled");
    CHECK(wb_quota_used(&l, EARTH) == 10, "earth reports ten used");
    CHECK(wb_quota_used(&l, ASIA) == 0,
          "a descendant's used does not move: nothing is held IN it");
    CHECK(wb_quota_available(&l, EARTH) == 0, "earth is full");
    CHECK(wb_quota_available(&l, ASIA) == 0,
          "and a full ancestor caps every descendant");
    CHECK(wb_quota_available(&l, CHINA) == 0, "three levels down, still capped");
    CHECK(wb_quota_reserve(&l, ASIA, B, 1) == -1,
          "a full planet refuses a reservation in its child");
    CHECK(wb_quota_used(&l, ASIA) == 0, "and the refusal changed nothing");

    CHECK(wb_quota_release(&l, EARTH, A, 4) == 0, "four units are released");
    CHECK(wb_quota_used(&l, EARTH) == 6, "earth has six used");
    CHECK(wb_quota_reserve(&l, ASIA, B, 1) == 0, "a child takes one unit");
    CHECK(wb_quota_used(&l, EARTH) == 7, "which spends the parent's capacity");
    CHECK(wb_quota_used(&l, ASIA) == 1, "and the child's own");
    CHECK(wb_quota_available(&l, CHINA) == 3,
          "the chain is now earth 3, asia 5, china 3 -- the bottleneck is 3");

    CHECK(wb_quota_reserve(&l, CHINA, C, 3) == 0, "china is filled");
    CHECK(wb_quota_used(&l, EARTH) == 10, "earth is full again");
    CHECK(wb_quota_used(&l, ASIA) == 4, "asia holds four");
    CHECK(wb_quota_used(&l, CHINA) == 3, "china holds three");
    CHECK(wb_quota_available(&l, CHINA) == 0, "china is full");
    CHECK(wb_quota_reserve(&l, CHINA, C, 1) == -1, "one more is refused");
    CHECK(wb_quota_reserve(&l, EUROPE, A, 1) == -1,
          "a full ancestor refuses a sibling subtree");
    CHECK(wb_quota_release(&l, CHINA, C, 3) == 0, "china is emptied");
    CHECK(wb_quota_used(&l, EARTH) == 7, "and the ancestors give the units back");
    CHECK(wb_quota_used(&l, ASIA) == 1, "asia keeps the one it held");
    CHECK(wb_quota_reserve(&l, EUROPE, A, 3) == 0, "europe takes three");
    CHECK(wb_quota_used(&l, EARTH) == 10, "earth is full once more");
    CHECK(wb_quota_available(&l, EUROPE) == 0, "so europe offers nothing");
    CHECK(wb_quota_available(&l, CRATER) == 2,
          "the moon chain was never touched");

    /* ---- a volume's own capacity can be the bottleneck ---- */
    wb_quota_ledger m;
    CHECK(build(&m) == 0, "a second fixture is built");
    CHECK(wb_quota_reserve(&m, MOON, A, 2) == 0, "the moon is filled");
    CHECK(wb_quota_used(&m, CRATER) == 0, "the crater holds nothing");
    CHECK(wb_quota_available(&m, CRATER) == 0,
          "but a full parent leaves a child nothing to offer");
    CHECK(wb_quota_reserve(&m, CRATER, B, 1) == -1,
          "a child cannot reserve past a full parent");

    /* ---- both a parent and a grandparent can be the bottleneck ---- */
    wb_quota_ledger n;
    CHECK(build(&n) == 0, "a third fixture is built");
    CHECK(wb_quota_reserve(&n, ASIA, A, 5) == 0, "asia takes five");
    CHECK(wb_quota_reserve(&n, CHINA, B, 1) == 0, "china takes one");
    CHECK(wb_quota_used(&n, ASIA) == 6, "asia is now full");
    CHECK(wb_quota_reserve(&n, CHINA, B, 1) == -1,
          "a full parent refuses even though the child has room");
    CHECK(wb_quota_reserve(&n, EARTH, B, 4) == 0, "earth takes four");
    CHECK(wb_quota_used(&n, EARTH) == 10, "earth is now full");
    CHECK(wb_quota_reserve(&n, EUROPE, B, 1) == -1,
          "a full grandparent refuses a fresh subtree");

    /* ---- release refuses ---- */
    wb_quota_ledger r;
    CHECK(build(&r) == 0, "a fourth fixture is built");
    CHECK(wb_quota_reserve(&r, EARTH, A, 3) == 0, "three units are reserved");
    uint64_t e0 = wb_quota_epoch(&r);
    CHECK(wb_quota_release(&r, EARTH, A, 4) == -1, "releasing too many is refused");
    CHECK(wb_quota_release(&r, EARTH, B, 1) == -1, "a non-holder cannot release");
    CHECK(wb_quota_release(&r, ASIA, A, 1) == -1,
          "a holder cannot release units reserved against another volume");
    CHECK(wb_quota_release(&r, 99, A, 1) == -1, "an unknown volume cannot release");
    CHECK(wb_quota_release(&r, EARTH, A, 0) == -1, "releasing zero is refused");
    CHECK(wb_quota_release(&r, EARTH, 0, 1) == -1, "holder 0 is refused");
    CHECK(wb_quota_release(NULL, EARTH, A, 1) == -1, "a NULL ledger refuses");
    CHECK(wb_quota_used(&r, EARTH) == 3, "no refusal moved the usage");
    CHECK(wb_quota_epoch(&r) == e0, "and no refusal advanced the epoch");
    CHECK(wb_quota_release(&r, EARTH, A, 3) == 0, "the real release succeeds");
    CHECK(wb_quota_used(&r, EARTH) == 0, "and empties the volume");
    CHECK(wb_quota_epoch(&r) == e0 + 1, "advancing the epoch once");
    CHECK(wb_quota_release(&r, EARTH, A, 1) == -1, "a second release is refused");

    /* ---- a quota covers a holder's subtree ---- */
    wb_quota_ledger q;
    CHECK(build(&q) == 0, "a fifth fixture is built");
    CHECK(wb_quota_set(&q, EARTH, A, 4) == 0, "a quota is set on earth");
    CHECK(wb_quota_limit(&q, EARTH, A) == 4, "and it reads back");
    CHECK(wb_quota_limit(&q, EARTH, B) == 0, "another holder has no quota");
    CHECK(wb_quota_reserve(&q, ASIA, A, 4) == 0, "the holder reserves four");
    CHECK(wb_quota_subtree_used(&q, EARTH, A) == 4, "all four are in earth's subtree");
    CHECK(wb_quota_subtree_used(&q, ASIA, A) == 4, "and in asia's");
    CHECK(wb_quota_holder_used(&q, ASIA, A) == 4, "reserved against asia itself");
    CHECK(wb_quota_holder_used(&q, EARTH, A) == 0, "none against earth itself");
    CHECK(wb_quota_reserve(&q, ASIA, A, 1) == -1, "one more would pass the quota");
    CHECK(wb_quota_reserve(&q, EUROPE, A, 1) == -1,
          "a sibling subtree is still inside earth's quota");
    CHECK(wb_quota_reserve(&q, EUROPE, B, 1) == 0, "another holder is unaffected");
    CHECK(wb_quota_subtree_used(&q, EARTH, B) == 1, "the other holder's usage");
    CHECK(wb_quota_subtree_used(&q, ASIA, B) == 0, "is not in asia's subtree");
    CHECK(wb_quota_set(&q, EARTH, A, 3) == -1,
          "a quota cannot be set below what the holder already holds");
    CHECK(wb_quota_set(&q, EARTH, A, 5) == 0, "it can be raised");
    CHECK(wb_quota_reserve(&q, EUROPE, A, 1) == 0, "and the holder can use the room");
    CHECK(wb_quota_subtree_used(&q, EARTH, A) == 5, "five of five are now used");
    CHECK(wb_quota_set(&q, EARTH, A, 0) == 0, "a zero limit clears the quota");
    CHECK(wb_quota_limit(&q, EARTH, A) == 0, "and it reads back as none");
    CHECK(wb_quota_reserve(&q, EUROPE, A, 1) == 0, "with no quota there is no cap");
    CHECK(wb_quota_subtree_used(&q, EARTH, A) == 6, "six units are held");

    /* ---- a quota on a mid-level volume covers its descendants ---- */
    wb_quota_ledger t;
    CHECK(build(&t) == 0, "a sixth fixture is built");
    CHECK(wb_quota_set(&t, ASIA, A, 2) == 0, "a quota is set on asia");
    CHECK(wb_quota_reserve(&t, CHINA, A, 2) == 0,
          "a descendant may use the allowance");
    CHECK(wb_quota_reserve(&t, CHINA, A, 1) == -1, "up to the cap");
    CHECK(wb_quota_reserve(&t, ASIA, A, 1) == -1,
          "the volume itself is inside its own subtree");
    CHECK(wb_quota_reserve(&t, EUROPE, A, 1) == 0,
          "a sibling of asia is outside asia's subtree");
    CHECK(wb_quota_subtree_used(&t, ASIA, A) == 2, "asia's subtree holds two");
    CHECK(wb_quota_subtree_used(&t, EARTH, A) == 3,
          "earth's subtree holds asia's two and europe's one");

    /* ---- set refuses bad input ---- */
    CHECK(wb_quota_set(&t, 99, A, 5) == -1, "an unknown volume refuses a quota");
    CHECK(wb_quota_set(&t, EARTH, 0, 5) == -1, "holder 0 is refused");
    CHECK(wb_quota_set(NULL, EARTH, A, 5) == -1, "a NULL ledger refuses");
    CHECK(wb_quota_limit(&t, 99, A) == 0, "an unknown volume has no quota");
    CHECK(wb_quota_holder_used(&t, 99, A) == 0, "and no usage");
    CHECK(wb_quota_subtree_used(&t, 99, A) == 0, "and no subtree usage");

    /* ---- the usage table has a ceiling ---- */
    wb_quota_ledger s;
    CHECK(wb_quota_init(&s, 4, 1) == 0, "a ledger with one usage slot");
    CHECK(wb_quota_add(&s, EARTH, 0, 10) == 0, "a volume is added");
    CHECK(wb_quota_reserve(&s, EARTH, A, 1) == 0, "the first holder reserves");
    CHECK(wb_quota_reserve(&s, EARTH, B, 1) == -1,
          "a second holder needs a slot the table does not have");
    CHECK(wb_quota_used(&s, EARTH) == 1, "and the refusal moved nothing");

    /* ---- the volume table has a ceiling ---- */
    wb_quota_ledger v;
    CHECK(wb_quota_init(&v, 2, 8) == 0, "a ledger with two volume slots");
    CHECK(wb_quota_add(&v, EARTH, 0, 10) == 0, "the first volume is added");
    CHECK(wb_quota_add(&v, ASIA, EARTH, 5) == 0, "the second volume is added");
    CHECK(wb_quota_add(&v, EUROPE, EARTH, 5) == -1, "the ceiling refuses a third");
    CHECK(wb_quota_count(&v) == 2, "and the ledger holds two");

    /* ---- degenerate input ---- */
    CHECK(wb_quota_init(NULL, 4, 4) == -1, "a NULL ledger is rejected");
    CHECK(wb_quota_init(&v, 0, 4) == -1, "a zero volume capacity is rejected");
    CHECK(wb_quota_init(&v, 4, 0) == -1, "a zero usage capacity is rejected");
    CHECK(wb_quota_count(NULL) == 0, "a NULL ledger has no volumes");
    CHECK(wb_quota_epoch(NULL) == 0, "a NULL ledger has no epoch");
    CHECK(wb_quota_capacity(NULL, EARTH) == 0, "a NULL ledger has no capacity");
    CHECK(wb_quota_used(NULL, EARTH) == 0, "no usage");
    CHECK(wb_quota_room(NULL, EARTH) == 0, "no free capacity");
    CHECK(wb_quota_available(NULL, EARTH) == 0, "nothing available");
    CHECK(wb_quota_limit(NULL, EARTH, A) == 0, "no quota");
    CHECK(wb_quota_holder_used(NULL, EARTH, A) == 0, "no holder usage");
    CHECK(wb_quota_subtree_used(NULL, EARTH, A) == 0, "no subtree usage");
    CHECK(wb_quota_add(NULL, 1, 0, 1) == -1, "a NULL ledger refuses a volume");
    CHECK(wb_quota_reserve(NULL, EARTH, A, 1) == -1, "and a reservation");
    CHECK(wb_quota_release(NULL, EARTH, A, 1) == -1, "and a release");
    CHECK(wb_quota_capacity(&v, 99) == 0, "an unknown volume has no capacity");
    CHECK(wb_quota_used(&v, 99) == 0, "no usage");
    CHECK(wb_quota_room(&v, 99) == 0, "no free capacity");
    CHECK(wb_quota_available(&v, 99) == 0, "nothing available");
    wb_quota_clear(&v);
    CHECK(wb_quota_count(&v) == 0, "a cleared ledger has no volumes");
    CHECK(wb_quota_epoch(&v) == 1, "and its epoch is reset");
    wb_quota_clear(NULL);
    wb_quota_free(&v);
    wb_quota_free(&s);
    wb_quota_free(&t);
    wb_quota_free(&q);
    wb_quota_free(&r);
    wb_quota_free(&n);
    wb_quota_free(&m);
    wb_quota_free(NULL);

    /* ---- determinism: the same operations agree ---- */
    wb_quota_ledger d1, d2;
    CHECK(build(&d1) == 0 && build(&d2) == 0, "two fixtures are built");
    for (int i = 0; i < 4; i++) {
        wb_quota_reserve(&d1, ASIA, A, 1);
        wb_quota_reserve(&d2, ASIA, A, 1);
    }
    wb_quota_reserve(&d1, EUROPE, B, 2);
    wb_quota_reserve(&d2, EUROPE, B, 2);
    CHECK(wb_quota_set(&d1, EARTH, A, 5) == 0, "a quota is set on the first ledger");
    CHECK(wb_quota_set(&d2, EARTH, A, 5) == 0, "and on the second");
    wb_quota_reserve(&d1, CHINA, A, 1);
    wb_quota_reserve(&d2, CHINA, A, 1);
    int same = wb_quota_used(&d1, EARTH) == wb_quota_used(&d2, EARTH) &&
               wb_quota_used(&d1, ASIA) == wb_quota_used(&d2, ASIA) &&
               wb_quota_available(&d1, CHINA) == wb_quota_available(&d2, CHINA) &&
               wb_quota_subtree_used(&d1, EARTH, A) ==
                   wb_quota_subtree_used(&d2, EARTH, A) &&
               wb_quota_epoch(&d1) == wb_quota_epoch(&d2);
    CHECK(same, "two ledgers given the same operations agree");
    wb_quota_free(&d1);
    wb_quota_free(&d2);

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

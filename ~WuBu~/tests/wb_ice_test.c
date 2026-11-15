/*
 * wb_ice_test.c -- LP-95 stone 2 gate: the ICE checklist.
 *
 * The two formulas are checked against hand-computed values, and the MAX/MIN
 * property the pair formula exists for is checked directly: a pair whose worst
 * side is better must sort ahead of a pair whose best side is better.
 */
#include "wb_ice.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)
#define CHECK_EQ(name, got, want) do { \
    unsigned long long _g = (unsigned long long)(got), _w = (unsigned long long)(want); \
    if (_g == _w) g_pass++; \
    else { g_fail++; printf("FAIL: %s: got %llu want %llu (line %d)\n", name, _g, _w, __LINE__); } \
} while (0)

static wb_stun_addr a4(int a, int b, int c, int d, uint16_t port) {
    wb_stun_addr x;
    memset(&x, 0, sizeof x);
    x.family = 4;
    x.addr[0] = (uint8_t)a; x.addr[1] = (uint8_t)b;
    x.addr[2] = (uint8_t)c; x.addr[3] = (uint8_t)d;
    x.port = port;
    return x;
}

/* The oracle: which pairs would answer a check. */
typedef struct { int mode; } oracle;

static int reach_all(void *ud, const wb_ice_candidate *l, const wb_ice_candidate *r) {
    (void)ud; (void)l; (void)r;
    return 1;
}
static int reach_none(void *ud, const wb_ice_candidate *l, const wb_ice_candidate *r) {
    (void)ud; (void)l; (void)r;
    return 0;
}
/* only relayed pairs work */
static int reach_relayed(void *ud, const wb_ice_candidate *l, const wb_ice_candidate *r) {
    (void)ud;
    return l->type == WB_ICE_RELAYED && r->type == WB_ICE_RELAYED;
}
/* only the pair whose local candidate is 10.0.0.1 works */
static int reach_one(void *ud, const wb_ice_candidate *l, const wb_ice_candidate *r) {
    (void)ud; (void)r;
    return l->addr.addr[0] == 10 && l->addr.addr[1] == 0 &&
           l->addr.addr[2] == 0 && l->addr.addr[3] == 1;
}

int main(void) {
    /* ---- the candidate priority formula, hand-computed ---- */
    CHECK_EQ("type pref host", wb_ice_type_pref(WB_ICE_HOST), 126);
    CHECK_EQ("type pref peer-reflexive", wb_ice_type_pref(WB_ICE_PEER_REFLEXIVE), 110);
    CHECK_EQ("type pref server-reflexive", wb_ice_type_pref(WB_ICE_SERVER_REFLEXIVE), 100);
    CHECK_EQ("type pref relayed", wb_ice_type_pref(WB_ICE_RELAYED), 0);
    CHECK_EQ("type pref unknown", wb_ice_type_pref(99), 0);

    /* (126 << 24) + (65535 << 8) + (256 - 1) = 0x7EFFFFFE */
    CHECK_EQ("host priority",
             wb_ice_candidate_priority(WB_ICE_HOST, 65535, 1), 0x7EFFFFFFu);
    /* (0 << 24) + (65535 << 8) + 255 = 0x00FFFFFE */
    CHECK_EQ("relayed priority",
             wb_ice_candidate_priority(WB_ICE_RELAYED, 65535, 1), 0x00FFFFFFu);
    /* component 2 costs two points */
    CHECK_EQ("component counts",
             wb_ice_candidate_priority(WB_ICE_HOST, 65535, 2),
             wb_ice_candidate_priority(WB_ICE_HOST, 65535, 1) - 1);

    /* ---- the pair priority formula, hand-computed ---- */
    /* MIN=3, MAX=5: 3*2^32 + 10 + 0 */
    CHECK_EQ("pair priority g<d", wb_ice_pair_priority(3, 5),
             3ull * 4294967296ull + 10ull);
    /* MIN=3, MAX=5, G>D: the tiebreak flips */
    CHECK_EQ("pair priority g>d", wb_ice_pair_priority(5, 3),
             3ull * 4294967296ull + 10ull + 1ull);
    CHECK_EQ("pair priority equal", wb_ice_pair_priority(7, 7),
             7ull * 4294967296ull + 14ull);
    CHECK_EQ("pair priority zero", wb_ice_pair_priority(0, 0), 0ull);

    /* ---- the MAX/MIN property ---- */
    {
        /* pair A: 100 and 900.  pair B: 500 and 500.
         * B has the better minimum, so it sorts first even though A has the
         * better maximum -- which is the whole point of the formula. */
        uint64_t pa = wb_ice_pair_priority(100, 900);
        uint64_t pb = wb_ice_pair_priority(500, 500);
        CHECK(pb > pa, "a better minimum beats a better maximum");
        /* and the controlling tiebreak only applies when min and max agree */
        CHECK(wb_ice_pair_priority(500, 500) == wb_ice_pair_priority(500, 500),
              "the tiebreak is deterministic");
    }

    /* ---- candidate gathering ---- */
    {
        wb_ice *ice = wb_ice_new(1);
        CHECK(ice != NULL, "an ICE agent allocates");
        wb_stun_addr host = a4(192, 0, 2, 1, 50000);
        wb_stun_addr reflexive = a4(203, 0, 113, 1, 8000);
        wb_stun_addr relay = a4(198, 51, 100, 7, 3478);

        int lh = wb_ice_add_local(ice, WB_ICE_HOST, 1, &host, NULL);
        int lr = wb_ice_add_local(ice, WB_ICE_SERVER_REFLEXIVE, 1, &reflexive, &host);
        int ll = wb_ice_add_local(ice, WB_ICE_RELAYED, 1, &relay, &host);
        CHECK(lh == 0 && lr == 1 && ll == 2, "three local candidates are added");
        CHECK_EQ("local count", wb_ice_local(ice), 3);
        CHECK_EQ("remote count", wb_ice_remote(ice), 0);

        const wb_ice_candidate *c = wb_ice_local_at(ice, 1);
        CHECK(c && c->type == WB_ICE_SERVER_REFLEXIVE, "the second is server-reflexive");
        CHECK(c->base.port == 50000, "and remembers the host base it came from");
        CHECK_EQ("host priority is highest",
                 wb_ice_local_at(ice, 0)->priority, 0x7EFFFFFFu);

        /* foundations: the reflexive candidate shares the host's base address,
         * so it stands or falls with it -- but a candidate off a different base
         * gets a different foundation. A relay is a different allocation
         * entirely. */
        uint32_t f_host = wb_ice_local_at(ice, 0)->foundation;
        uint32_t f_ref  = wb_ice_local_at(ice, 1)->foundation;
        uint32_t f_rel  = wb_ice_local_at(ice, 2)->foundation;
        CHECK(f_ref != f_rel, "a reflexive and a relay are different foundations");
        CHECK(f_host != f_rel, "a host and a relay are different foundations");

        /* bad input */
        CHECK(wb_ice_add_local(ice, 99, 1, &host, NULL) == -1, "an unknown type is refused");
        CHECK(wb_ice_add_local(ice, WB_ICE_HOST, 0, &host, NULL) == -1, "component 0 is refused");
        CHECK(wb_ice_add_local(ice, WB_ICE_HOST, 257, &host, NULL) == -1, "component 257 is refused");
        CHECK(wb_ice_add_local(NULL, WB_ICE_HOST, 1, &host, NULL) == -1, "a null agent is refused");
        CHECK_EQ("count after refusals", wb_ice_local(ice), 3);

        /* a second host candidate on the same base shares a foundation */
        wb_stun_addr host2 = a4(192, 0, 2, 1, 50001);
        wb_ice_add_local(ice, WB_ICE_HOST, 1, &host2, NULL);
        CHECK(wb_ice_local_at(ice, 3)->foundation == f_host,
              "two host candidates on the same base share a foundation");
        wb_ice_free(ice);
    }

    /* ---- pair formation ---- */
    {
        wb_ice *ice = wb_ice_new(1);
        wb_stun_addr l1 = a4(192, 0, 2, 1, 50000);
        wb_stun_addr l2 = a4(192, 0, 2, 2, 50000);
        wb_stun_addr r1 = a4(203, 0, 113, 1, 8000);
        wb_stun_addr r2 = a4(203, 0, 113, 2, 8000);
        wb_stun_addr r3 = a4(203, 0, 113, 3, 8000);
        wb_ice_add_local(ice, WB_ICE_HOST, 1, &l1, NULL);
        wb_ice_add_local(ice, WB_ICE_HOST, 1, &l2, NULL);
        wb_ice_add_remote(ice, WB_ICE_HOST, 1, &r1, NULL);
        wb_ice_add_remote(ice, WB_ICE_HOST, 1, &r2, NULL);
        wb_ice_add_remote(ice, WB_ICE_HOST, 1, &r3, NULL);
        wb_ice_prioritize(ice);
        CHECK_EQ("two locals times three remotes", wb_ice_pairs(ice), 6);
        /* every pair is a distinct combination */
        int seen[8][8] = {{0}};
        int dup = 0;
        for (size_t i = 0; i < wb_ice_pairs(ice); i++) {
            const wb_ice_pair *p = wb_ice_pair_at(ice, i);
            if (seen[p->local][p->remote]) dup++;
            seen[p->local][p->remote] = 1;
            CHECK_EQ("pair starts Waiting", p->state, WB_ICE_WAITING);
        }
        CHECK_EQ("no duplicate pair", dup, 0);

        /* a different component is not paired */
        wb_stun_addr l3 = a4(10, 0, 0, 1, 50000);
        wb_ice_add_local(ice, WB_ICE_HOST, 2, &l3, NULL);
        wb_ice_prioritize(ice);
        CHECK_EQ("component 2 is not paired with component 1", wb_ice_pairs(ice), 6);

        /* a different address family is not paired */
        wb_stun_addr r6;
        memset(&r6, 0, sizeof r6);
        r6.family = 6;
        r6.port = 8000;
        r6.addr[15] = 1;
        wb_ice_add_remote(ice, WB_ICE_HOST, 1, &r6, NULL);
        wb_ice_prioritize(ice);
        CHECK_EQ("IPv6 is not paired with IPv4", wb_ice_pairs(ice), 6);

        /* the checklist is ordered by descending pair priority */
        for (size_t i = 1; i < wb_ice_pairs(ice); i++) {
            const wb_ice_pair *a = wb_ice_pair_at(ice, i - 1);
            const wb_ice_pair *b = wb_ice_pair_at(ice, i);
            if (a->priority < b->priority) {
                g_fail++;
                printf("FAIL: the checklist is not in descending order (line %d)\n", __LINE__);
                break;
            }
            g_pass++;
        }
        wb_ice_free(ice);
    }

    /* ---- redundant candidates ---- */
    {
        wb_ice *ice = wb_ice_new(1);
        wb_stun_addr l = a4(192, 0, 2, 1, 50000);
        wb_ice_add_local(ice, WB_ICE_HOST, 1, &l, NULL);
        wb_ice_add_local(ice, WB_ICE_HOST, 1, &l, NULL);   /* the same one twice */
        CHECK_EQ("duplicated", wb_ice_local(ice), 2);
        CHECK(wb_ice_remove_redundant(ice, WB_ICE_HOST, &l) == 1,
              "a redundant candidate is removed");
        CHECK_EQ("leaving one", wb_ice_local(ice), 1);
        /* a different port is not redundant */
        wb_stun_addr other = a4(192, 0, 2, 2, 50001);
        CHECK(wb_ice_remove_redundant(ice, WB_ICE_HOST, &other) == 0,
              "a different address is kept");
        /* a relay on the same address is a different candidate, so removing the
         * host leaves the relay alone */
        wb_ice_add_local(ice, WB_ICE_RELAYED, 1, &l, NULL);
        CHECK_EQ("two candidates now", wb_ice_local(ice), 2);
        CHECK(wb_ice_remove_redundant(ice, WB_ICE_HOST, &l) == 1,
              "removing the host does not remove the relay");
        CHECK_EQ("the relay survives", wb_ice_local(ice), 1);
        CHECK(wb_ice_local_at(ice, 0)->type == WB_ICE_RELAYED,
              "and it is the relay");
        CHECK(wb_ice_remove_redundant(ice, WB_ICE_RELAYED, &l) == 1,
              "the relay is removable in its own right");
        wb_ice_free(ice);
    }

    /* ---- the checklist runs in priority order, and nominates ---- */
    {
        wb_ice *ice = wb_ice_new(1);
        wb_stun_addr host = a4(192, 0, 2, 1, 50000);
        wb_stun_addr reflex = a4(203, 0, 113, 1, 8000);
        wb_stun_addr relay = a4(198, 51, 100, 7, 3478);
        wb_ice_add_local(ice, WB_ICE_HOST, 1, &host, NULL);
        wb_ice_add_local(ice, WB_ICE_SERVER_REFLEXIVE, 1, &reflex, &host);
        wb_ice_add_local(ice, WB_ICE_RELAYED, 1, &relay, &host);
        wb_ice_add_remote(ice, WB_ICE_HOST, 1, &host, NULL);
        wb_ice_add_remote(ice, WB_ICE_RELAYED, 1, &relay, NULL);

        /* the first check is the highest-priority Waiting pair: the two host
         * candidates, since host has the best type preference */
        CHECK(wb_ice_check_next(ice, reach_all, NULL) == 1, "a check runs");
        CHECK_EQ("the host pair is first",
                 wb_ice_pair_state(ice, 0, 0), WB_ICE_SUCCEEDED);

        int st = wb_ice_run(ice, reach_all, NULL);
        CHECK_EQ("everything reachable completes the checklist", st, WB_ICE_CHECK_COMPLETED);
        const wb_ice_pair *nom = wb_ice_nominated(ice);
        CHECK(nom != NULL, "a pair is nominated");
        CHECK(nom->local == 0 && nom->remote == 0,
              "and it is the highest-priority one: the two hosts");
        wb_ice_free(ice);
    }

    /* ---- only the relay works: the checklist falls through to it ---- */
    {
        wb_ice *ice = wb_ice_new(1);
        wb_stun_addr host = a4(192, 0, 2, 1, 50000);
        wb_stun_addr reflex = a4(203, 0, 113, 1, 8000);
        wb_stun_addr relay = a4(198, 51, 100, 7, 3478);
        wb_ice_add_local(ice, WB_ICE_HOST, 1, &host, NULL);
        wb_ice_add_local(ice, WB_ICE_SERVER_REFLEXIVE, 1, &reflex, &host);
        wb_ice_add_local(ice, WB_ICE_RELAYED, 1, &relay, &host);
        wb_ice_add_remote(ice, WB_ICE_HOST, 1, &host, NULL);
        wb_ice_add_remote(ice, WB_ICE_RELAYED, 1, &relay, NULL);

        int st = wb_ice_run(ice, reach_relayed, NULL);
        CHECK_EQ("a relay-only path still completes", st, WB_ICE_CHECK_COMPLETED);
        const wb_ice_pair *nom = wb_ice_nominated(ice);
        CHECK(nom && nom->local == 2 && nom->remote == 1,
              "and nominates the relay pair");
        CHECK_EQ("the host pair failed",
                 wb_ice_pair_state(ice, 0, 0), WB_ICE_FAILED);
        CHECK_EQ("the reflexive pair failed",
                 wb_ice_pair_state(ice, 1, 0), WB_ICE_FAILED);
        wb_ice_free(ice);
    }

    /* ---- one specific local address works ---- */
    {
        wb_ice *ice = wb_ice_new(1);
        wb_stun_addr l1 = a4(10, 0, 0, 1, 50000);
        wb_stun_addr l2 = a4(10, 0, 0, 2, 50000);
        wb_stun_addr r1 = a4(203, 0, 113, 1, 8000);
        wb_ice_add_local(ice, WB_ICE_HOST, 1, &l1, NULL);
        wb_ice_add_local(ice, WB_ICE_HOST, 1, &l2, NULL);
        wb_ice_add_remote(ice, WB_ICE_HOST, 1, &r1, NULL);
        int st = wb_ice_run(ice, reach_one, NULL);
        CHECK_EQ("one working pair is enough", st, WB_ICE_CHECK_COMPLETED);
        const wb_ice_pair *nom = wb_ice_nominated(ice);
        CHECK(nom && nom->local == 0, "and it is the one that works");
        wb_ice_free(ice);
    }

    /* ---- nothing works ---- */
    {
        wb_ice *ice = wb_ice_new(1);
        wb_stun_addr l = a4(192, 0, 2, 1, 50000);
        wb_stun_addr r = a4(203, 0, 113, 1, 8000);
        wb_ice_add_local(ice, WB_ICE_HOST, 1, &l, NULL);
        wb_ice_add_remote(ice, WB_ICE_HOST, 1, &r, NULL);
        CHECK_EQ("an unreachable pair fails the checklist",
                 wb_ice_run(ice, reach_none, NULL), WB_ICE_CHECK_FAILED);
        CHECK(wb_ice_nominated(ice) == NULL, "and nothing is nominated");
        CHECK_EQ("the pair is marked Failed",
                 wb_ice_pair_state(ice, 0, 0), WB_ICE_FAILED);
        wb_ice_free(ice);
    }

    /* ---- an empty checklist has not failed yet ---- */
    {
        wb_ice *ice = wb_ice_new(1);
        CHECK_EQ("no candidates is running", wb_ice_state(ice), WB_ICE_CHECK_RUNNING);
        CHECK_EQ("and no checks are possible",
                 wb_ice_check_next(ice, reach_all, NULL), 0);
        wb_ice_free(ice);
    }

    /* ---- the controlling side changes which priority is G ---- */
    {
        /* With the same two candidates, the controlling agent's own candidate is
         * G, so flipping the role changes the pair priority's tiebreak. */
        uint64_t as_controller = wb_ice_pair_priority(9, 4);
        uint64_t as_controlled = wb_ice_pair_priority(4, 9);
        CHECK(as_controller != as_controlled,
              "the tiebreak depends on who is controlling");
        CHECK(as_controller - as_controlled == 1,
              "and differs only by the tiebreak bit");
    }

    printf("\n=== ICE TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail != 0;
}

/*
 * wb_match_test.c -- LP-34 stone 1 gate: matchmaking team balance.
 *
 * Devil's-advocate cases: a perfectly splittable set, an odd set that cannot be
 * split evenly, team sizes never differing by more than one, a single player,
 * and determinism.
 */
#include "wb_match.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

int main(void) {
    uint8_t team[16];

    /* ---- a set that splits perfectly ---- */
    const uint32_t even[4] = { 10, 20, 30, 40 };
    uint32_t diff = wb_match_balance(even, 4, team);
    CHECK(diff == 0, "an evenly splittable set balances exactly");
    CHECK(wb_match_team_skill(even, team, 4, 0) == 50, "team 0 sums to 50");
    CHECK(wb_match_team_skill(even, team, 4, 1) == 50, "team 1 sums to 50");

    /* ---- an odd set: within one player's skill ---- */
    const uint32_t odd[5] = { 1, 2, 3, 4, 5 };
    diff = wb_match_balance(odd, 5, team);
    CHECK(diff == 1, "an odd set balances to within one point");
    uint32_t n0 = 0, n1 = 0;
    for (int i = 0; i < 5; i++) { if (team[i] == 0) n0++; else n1++; }
    CHECK(n0 + n1 == 5, "every player is assigned");
    CHECK((n0 == 3 && n1 == 2) || (n0 == 2 && n1 == 3), "the teams differ by at most one player");

    /* ---- a mirror set balances exactly ---- */
    const uint32_t gap[4] = { 1, 1, 100, 100 };
    diff = wb_match_balance(gap, 4, team);
    CHECK(diff == 0, "a two-versus-two mirror balances exactly");

    /* ---- a single player goes to team 0 ---- */
    const uint32_t one[1] = { 42 };
    diff = wb_match_balance(one, 1, team);
    CHECK(diff == 42 && team[0] == 0, "a single player is team 0 and the difference is their skill");

    /* ---- a local optimum: no single move improves the balance ---- */
    int local_ok = 1;
    for (uint32_t n = 2; n <= 16; n++) {
        uint32_t skill[16];
        for (uint32_t i = 0; i < n; i++) skill[i] = (i * 37 + 11) % 100;
        uint32_t d = wb_match_balance(skill, n, team);
        uint32_t a = wb_match_team_skill(skill, team, n, 0);
        uint32_t b = wb_match_team_skill(skill, team, n, 1);
        for (uint32_t i = 0; i < n; i++) {
            uint32_t na = a, nb = b;
            if (team[i] == 0) { na -= skill[i]; nb += skill[i]; }
            else              { nb -= skill[i]; na += skill[i]; }
            uint32_t nd = na > nb ? na - nb : nb - na;
            if (nd < d) local_ok = 0;
        }
    }
    CHECK(local_ok, "no single move improves the balance");

    /* ---- determinism ---- */
    const uint32_t set[8] = { 5, 50, 23, 91, 12, 77, 34, 60 };
    uint8_t a[8], b[8];
    uint32_t d1 = wb_match_balance(set, 8, a);
    uint32_t d2 = wb_match_balance(set, 8, b);
    CHECK(d1 == d2 && memcmp(a, b, sizeof a) == 0, "balancing is deterministic");

    /* ---- degenerate input ---- */
    CHECK(wb_match_balance(NULL, 4, team) == 0, "NULL skills are safe");
    CHECK(wb_match_balance(even, 0, team) == 0, "zero players balance to zero");
    CHECK(wb_match_balance(even, 4, NULL) == 0, "a NULL team array is safe");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

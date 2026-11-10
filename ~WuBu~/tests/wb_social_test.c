/*
 * wb_social_test.c -- LP-51 stone 1 gate: the social graph.
 *
 * Devil's-advocate cases: a one-way friend bit is a request and not a friendship,
 * blocking pins trust at zero and clears the friendship it overwrote, a friend of
 * a friend is not a friend, and the friend list is emitted in ascending order
 * regardless of the order edges were added.
 */
#include "wb_social.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

#define N 8

int main(void) {
    wb_social g;
    CHECK(wb_social_init(&g, N, 16) == 0, "the graph is created");

    /* ---- set / add / remove / has ---- */
    CHECK(wb_social_set(&g, 0, 1, WB_SOCIAL_FRIEND) == 0, "an edge is set");
    CHECK(wb_social_has(&g, 0, 1, WB_SOCIAL_FRIEND) == 1, "the flag is present");
    CHECK(wb_social_has(&g, 0, 1, WB_SOCIAL_BLOCKED) == 0, "an unset flag is absent");
    CHECK(wb_social_flags(&g, 0, 1) == WB_SOCIAL_FRIEND, "the raw flags read back");
    CHECK(wb_social_add(&g, 0, 1, WB_SOCIAL_PENDING) == (int)(WB_SOCIAL_FRIEND | WB_SOCIAL_PENDING),
          "flags are added");
    CHECK(wb_social_remove(&g, 0, 1, WB_SOCIAL_PENDING) == (int)WB_SOCIAL_FRIEND,
          "flags are removed");
    CHECK(wb_social_flags(&g, 1, 0) == 0, "the reverse edge is independent");

    /* ---- out-of-range and self edges ---- */
    CHECK(wb_social_set(&g, 0, N, WB_SOCIAL_FRIEND) == -1, "an out-of-range user is refused");
    CHECK(wb_social_set(&g, N, 0, WB_SOCIAL_FRIEND) == -1, "an out-of-range other is refused");
    CHECK(wb_social_set(&g, 0, 0, WB_SOCIAL_FRIEND) == -1, "a self edge is refused");
    CHECK(wb_social_add(&g, 0, N, WB_SOCIAL_FRIEND) == 0, "an out-of-range add is a no-op");
    CHECK(wb_social_remove(&g, 0, N, WB_SOCIAL_FRIEND) == 0, "an out-of-range remove is a no-op");
    CHECK(wb_social_has(&g, 0, N, WB_SOCIAL_FRIEND) == 0, "an out-of-range has is false");

    /* ---- friendship is mutual ---- */
    wb_social_clear(&g);
    CHECK(wb_social_count(&g) == 0, "clear drops every edge");
    wb_social_set(&g, 0, 1, WB_SOCIAL_FRIEND);       /* one way only */
    CHECK(wb_social_are_friends(&g, 0, 1) == 0, "a one-way friend bit is not a friendship");
    wb_social_set(&g, 1, 0, WB_SOCIAL_FRIEND);
    CHECK(wb_social_are_friends(&g, 0, 1) == 1, "both directions make a friendship");
    CHECK(wb_social_are_friends(&g, 1, 0) == 1, "friendship is symmetric");

    /* ---- the fixture ---- */
    wb_social_clear(&g);
    wb_social_set(&g, 0, 1, WB_SOCIAL_FRIEND);
    wb_social_set(&g, 1, 0, WB_SOCIAL_FRIEND);
    wb_social_set(&g, 1, 2, WB_SOCIAL_FRIEND);
    wb_social_set(&g, 2, 1, WB_SOCIAL_FRIEND);
    wb_social_set(&g, 0, 4, WB_SOCIAL_FRIEND);       /* a request 0->4, not mutual */
    wb_social_set(&g, 5, 6, WB_SOCIAL_FRIEND);
    wb_social_set(&g, 6, 5, WB_SOCIAL_FRIEND);

    /* ---- the friend list is ascending regardless of insertion order ---- */
    uint32_t fr[8];
    CHECK(wb_social_friends(&g, 0, fr, 8) == 1 && fr[0] == 1, "0 has exactly one friend");
    CHECK(wb_social_friends(&g, 1, fr, 8) == 2 && fr[0] == 0 && fr[1] == 2, "1's friends are ascending");
    CHECK(wb_social_friends(&g, 3, fr, 8) == 0, "3 has no friends");
    CHECK(wb_social_friends(&g, 0, fr, 1) == 1 && fr[0] == 1, "a one-slot buffer returns one friend");

    /* ---- trust ranks ---- */
    CHECK(wb_social_trust(&g, 0, 1) == WB_SOCIAL_TRUST_FRIEND, "a direct friend is trust 1");
    CHECK(wb_social_trust(&g, 0, 2) == WB_SOCIAL_TRUST_FOF, "a friend of a friend is trust 2");
    CHECK(wb_social_trust(&g, 0, 3) == WB_SOCIAL_TRUST_STRANGER, "a stranger is trust 3");
    CHECK(wb_social_trust(&g, 0, 4) == WB_SOCIAL_TRUST_STRANGER, "a one-way request is a stranger");
    CHECK(wb_social_trust(&g, 0, 0) == WB_SOCIAL_TRUST_FRIEND, "a user is their own friend");

    /* ---- blocking ---- */
    CHECK(wb_social_block(&g, 0, 1) == 0, "a block is applied");
    CHECK(wb_social_blocked(&g, 0, 1) == 1, "the block is visible from the blocker");
    CHECK(wb_social_blocked(&g, 1, 0) == 1, "the block is visible from the blocked");
    CHECK(wb_social_trust(&g, 0, 1) == WB_SOCIAL_TRUST_BLOCKED, "a block pins trust at zero");
    CHECK(wb_social_trust(&g, 1, 0) == WB_SOCIAL_TRUST_BLOCKED, "a block pins trust both ways");
    CHECK(wb_social_are_friends(&g, 0, 1) == 0, "blocking clears the friendship");
    CHECK(wb_social_flags(&g, 0, 1) == WB_SOCIAL_BLOCKED, "the block edge carries exactly BLOCKED");
    CHECK(wb_social_flags(&g, 1, 0) == 0, "the reverse edge lost its friendship");

    /* a pending request is cleared by a block */
    wb_social_set(&g, 0, 4, WB_SOCIAL_FRIEND | WB_SOCIAL_PENDING);
    wb_social_block(&g, 4, 0);
    CHECK(wb_social_flags(&g, 0, 4) == 0, "a block clears the pending request on the reverse edge");
    CHECK(wb_social_flags(&g, 4, 0) == WB_SOCIAL_BLOCKED, "the blocker's edge is exactly BLOCKED");

    /* ---- a full graph refuses to grow ---- */
    wb_social small;
    wb_social_init(&small, 4, 2);
    CHECK(wb_social_set(&small, 0, 1, WB_SOCIAL_FRIEND) == 0, "the first edge fits");
    CHECK(wb_social_set(&small, 1, 0, WB_SOCIAL_FRIEND) == 0, "the second edge fits");
    CHECK(wb_social_set(&small, 2, 3, WB_SOCIAL_FRIEND) == -1, "a full graph refuses to grow");

    /* ---- clear ---- */
    wb_social_clear(&g);
    CHECK(wb_social_count(&g) == 0, "clear empties the graph");
    CHECK(wb_social_friends(&g, 0, fr, 8) == 0, "a cleared graph has no friends");
    CHECK(wb_social_trust(&g, 0, 1) == WB_SOCIAL_TRUST_STRANGER, "a cleared graph is all strangers");

    /* ---- determinism ---- */
    wb_social a, b;
    wb_social_init(&a, N, 16);
    wb_social_init(&b, N, 16);
    for (int u = 0; u < 4; u++) {
        wb_social_set(&a, (uint32_t)u, (uint32_t)((u + 1) % 4), WB_SOCIAL_FRIEND);
        wb_social_set(&b, (uint32_t)u, (uint32_t)((u + 1) % 4), WB_SOCIAL_FRIEND);
    }
    int same = 1;
    for (uint32_t u = 0; u < N; u++) {
        uint32_t fa[8], fb[8];
        uint32_t ca = wb_social_friends(&a, u, fa, 8);
        uint32_t cb = wb_social_friends(&b, u, fb, 8);
        same &= (ca == cb);
        if (ca) same &= (memcmp(fa, fb, (size_t)ca * sizeof fa[0]) == 0);
        for (uint32_t v = 0; v < N; v++) same &= (wb_social_trust(&a, u, v) == wb_social_trust(&b, u, v));
    }
    CHECK(same, "the graph is deterministic");

    /* ---- degenerate input ---- */
    CHECK(wb_social_init(NULL, 4, 4) == -1, "a NULL graph is rejected");
    CHECK(wb_social_init(&small, 4, 0) == -1, "a zero capacity is rejected");
    CHECK(wb_social_set(NULL, 0, 1, WB_SOCIAL_FRIEND) == -1, "a NULL graph refuses a set");
    CHECK(wb_social_add(NULL, 0, 1, WB_SOCIAL_FRIEND) == 0, "a NULL graph add is a no-op");
    CHECK(wb_social_remove(NULL, 0, 1, WB_SOCIAL_FRIEND) == 0, "a NULL graph remove is a no-op");
    CHECK(wb_social_has(NULL, 0, 1, WB_SOCIAL_FRIEND) == 0, "a NULL graph has no flags");
    CHECK(wb_social_flags(NULL, 0, 1) == 0, "a NULL graph reports no edge");
    CHECK(wb_social_are_friends(NULL, 0, 1) == 0, "a NULL graph has no friends");
    CHECK(wb_social_blocked(NULL, 0, 1) == 0, "a NULL graph has no blocks");
    CHECK(wb_social_block(NULL, 0, 1) == -1, "a NULL graph refuses a block");
    CHECK(wb_social_friends(NULL, 0, fr, 8) == 0, "a NULL graph lists no friends");
    CHECK(wb_social_trust(NULL, 0, 1) == WB_SOCIAL_TRUST_STRANGER, "a NULL graph is all strangers");
    CHECK(wb_social_count(NULL) == 0, "a NULL graph has no edges");

    wb_social_free(&g);
    wb_social_free(&small);
    wb_social_free(&a);
    wb_social_free(&b);
    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

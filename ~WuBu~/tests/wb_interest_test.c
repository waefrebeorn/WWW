/*
 * wb_interest_test.c -- LP-103 stone 1 gate: parent-child interest propagation.
 *
 * Devil's-advocate cases: a set containing an ANCESTOR covers a descendant, which
 * is the whole point of a parent-cell AOI; aggregate() must not duplicate the root
 * when many cells share it; refine() of the root yields all four halves and
 * refine() of a level-1 cell yields its four children, not the root's; coarsen()
 * DROPS members shallower than the target level instead of inventing a deeper
 * cell; and a duplicate add changes nothing.
 */
#include "wb_interest.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static int has(const wb_interest_set *s, const char *cell) {
    for (uint32_t i = 0; i < s->n; i++)
        if (strcmp(s->cells[i], cell) == 0) return 1;
    return 0;
}

static wb_interest_set mk(int n, const char **cells) {
    wb_interest_set s;
    wb_interest_init(&s);
    for (int i = 0; i < n; i++) wb_interest_add(&s, cells[i]);
    return s;
}

int main(void) {
    wb_interest_set s, out;

    /* ---- add and dedupe ---- */
    wb_interest_init(&s);
    CHECK(wb_interest_count(&s) == 0, "a fresh set is empty");
    CHECK(wb_interest_add(&s, "0123") == 0, "a cell is added");
    CHECK(wb_interest_count(&s) == 1, "the count is one");
    CHECK(wb_interest_add(&s, "0123") == 0, "a duplicate add is accepted");
    CHECK(wb_interest_count(&s) == 1, "and changes nothing");
    CHECK(wb_interest_add(&s, "01") == 0, "an ancestor is added");
    CHECK(wb_interest_count(&s) == 2, "the count is two");
    CHECK(wb_interest_add(&s, "4") == -1, "an invalid cell is refused");
    CHECK(wb_interest_add(&s, NULL) == -1, "a NULL cell is refused");
    CHECK(wb_interest_add(NULL, "0") == -1, "a NULL set is refused");

    /* ---- coverage: a member or an ancestor covers ---- */
    const char *cells[] = { "01" };
    s = mk(1, cells);
    CHECK(wb_interest_covers(&s, "01") == 1, "a member covers itself");
    CHECK(wb_interest_covers(&s, "0123") == 1, "an ancestor covers a descendant");
    CHECK(wb_interest_covers(&s, "012") == 1, "an ancestor covers a shallow descendant");
    CHECK(wb_interest_covers(&s, "02") == 0, "a sibling is not covered");
    CHECK(wb_interest_covers(&s, "0") == 0, "a parent is not covered by its child");
    CHECK(wb_interest_covers(&s, "1") == 0, "an uncle is not covered");
    CHECK(wb_interest_covers(&s, "") == 0, "a parent is NOT covered by its child");
    {
        wb_interest_set empty = mk(0, NULL);
        CHECK(wb_interest_covers(&empty, "0") == 0, "an empty set covers nothing");
    }
    CHECK(wb_interest_covers(NULL, "0") == 0, "a NULL set covers nothing");
    {
        const char *root[] = { "" };
        s = mk(1, root);
        CHECK(wb_interest_covers(&s, "0123") == 1, "the root covers the whole world");
        CHECK(wb_interest_covers(&s, "") == 1, "the root covers itself");
    }

    /* ---- aggregate: push interest UP ---- */
    {
        const char *two[] = { "0123", "0201" };
        s = mk(2, two);
        CHECK(wb_interest_aggregate(&s, &out) == 0, "aggregating succeeds");
        CHECK(wb_interest_count(&out) == 8, "the set grew by the ancestors (5 + 3 new)");
        CHECK(has(&out, "0123") && has(&out, "0201"), "the members survive");
        CHECK(has(&out, "012") && has(&out, "020"), "their parents were added");
        CHECK(has(&out, "01") && has(&out, "02"), "their grandparents were added");
        CHECK(has(&out, "0"), "the shared great-grandparent was added once");
        CHECK(has(&out, ""), "the root was added");
        CHECK(wb_interest_count(&out) == 8, "the shared root was added exactly once");
    }
    {
        const char *leaf[] = { "0123" };
        s = mk(1, leaf);
        wb_interest_aggregate(&s, &out);
        CHECK(wb_interest_count(&out) == 5, "a single leaf aggregates to a chain of five");
        CHECK(has(&out, "0123") && has(&out, "012") && has(&out, "01") && has(&out, "0") && has(&out, ""),
              "the whole ancestor chain is present");
    }
    CHECK(wb_interest_aggregate(NULL, &out) == -1, "a NULL set is refused");
    CHECK(wb_interest_aggregate(&s, NULL) == -1, "a NULL output is refused");

    /* ---- refine: push interest DOWN one level ---- */
    {
        const char *root[] = { "" };
        s = mk(1, root);
        CHECK(wb_interest_refine(&s, &out) == 0, "refining the root succeeds");
        CHECK(wb_interest_count(&out) == 4, "the root has four children");
        CHECK(has(&out, "0") && has(&out, "1") && has(&out, "2") && has(&out, "3"), "all four halves");
    }
    {
        const char *one[] = { "0" };
        s = mk(1, one);
        wb_interest_refine(&s, &out);
        CHECK(wb_interest_count(&out) == 4, "a half has four children");
        CHECK(has(&out, "00") && has(&out, "01") && has(&out, "02") && has(&out, "03"),
              "the children are the level-2 cells under it");
        CHECK(!has(&out, "10"), "and not the cells under a sibling");
    }
    {
        const char *two[] = { "01", "02" };
        s = mk(2, two);
        wb_interest_refine(&s, &out);
        CHECK(wb_interest_count(&out) == 8, "two cells refine to eight children");
    }
    CHECK(wb_interest_refine(NULL, &out) == -1, "a NULL set is refused");
    CHECK(wb_interest_refine(&s, NULL) == -1, "a NULL output is refused");

    /* ---- coarsen: the ancestor at exactly a level ---- */
    {
        const char *deep[] = { "0123", "0201" };
        s = mk(2, deep);
        CHECK(wb_interest_coarsen(&s, 2, &out) == 0, "coarsening succeeds");
        CHECK(wb_interest_count(&out) == 2, "two distinct level-2 ancestors");
        CHECK(has(&out, "01") && has(&out, "02"), "and they are the level-2 parents");
    }
    {
        s = mk(2, (const char *[]){ "0123", "0201" });
        wb_interest_coarsen(&s, 4, &out);
        CHECK(wb_interest_count(&out) == 2 && has(&out, "0123") && has(&out, "0201"),
              "coarsening to the cells' own level is a no-op");
    }
    {
        s = mk(2, (const char *[]){ "0123", "0201" });
        wb_interest_coarsen(&s, 0, &out);
        CHECK(wb_interest_count(&out) == 1 && has(&out, ""), "coarsening to the root yields the root");
    }
    {
        /* members shallower than the target level are DROPPED, not deepened */
        s = mk(2, (const char *[]){ "0", "0123" });
        wb_interest_coarsen(&s, 2, &out);
        CHECK(wb_interest_count(&out) == 1 && has(&out, "01"),
              "a member shallower than the target level is dropped");
    }
    CHECK(wb_interest_coarsen(NULL, 2, &out) == -1, "a NULL set is refused");
    CHECK(wb_interest_coarsen(&s, 2, NULL) == -1, "a NULL output is refused");

    /* ---- equality ---- */
    {
        wb_interest_set a = mk(2, (const char *[]){ "01", "02" });
        wb_interest_set b = mk(2, (const char *[]){ "01", "02" });
        wb_interest_set c = mk(2, (const char *[]){ "02", "01" });
        wb_interest_set d = mk(1, (const char *[]){ "01" });
        CHECK(wb_interest_equal(&a, &b) == 1, "identical sets are equal");
        CHECK(wb_interest_equal(&a, &c) == 0, "order matters: the sets are built in insertion order");
        CHECK(wb_interest_equal(&a, &d) == 0, "different sizes are not equal");
        CHECK(wb_interest_equal(NULL, &a) == 0, "a NULL set equals nothing");
    }

    /* ---- determinism ---- */
    {
        wb_interest_set a = mk(3, (const char *[]){ "0123", "0201", "3000" });
        wb_interest_set b = mk(3, (const char *[]){ "0123", "0201", "3000" });
        wb_interest_set aa, ab;
        wb_interest_aggregate(&a, &aa);
        wb_interest_aggregate(&b, &ab);
        CHECK(wb_interest_equal(&aa, &ab) == 1, "aggregation is deterministic");
        wb_interest_refine(&aa, &out);
        CHECK(wb_interest_count(&out) == 48, "refining twelve aggregated cells yields 48 children");
    }
    {
        wb_interest_set a = mk(2, (const char *[]){ "0123", "0201" });
        wb_interest_set b = mk(2, (const char *[]){ "0123", "0201" });
        wb_interest_set ca, cb;
        wb_interest_coarsen(&a, 2, &ca);
        wb_interest_coarsen(&b, 2, &cb);
        CHECK(wb_interest_equal(&ca, &cb) == 1, "coarsening is deterministic");
    }

    /* ---- degenerate input ---- */
    wb_interest_init(NULL);
    CHECK(wb_interest_count(NULL) == 0, "a NULL set has no members");
    CHECK(wb_interest_covers(NULL, "0") == 0, "a NULL set covers nothing");
    {
        wb_interest_set full;
        wb_interest_init(&full);
        for (int i = 0; i < WB_INTEREST_MAX; i++) {
            /* 64 distinct three-digit cells, one per index */
            char c[4];
            int v = i;
            for (int k = 2; k >= 0; k--) { c[k] = (char)('0' + (v & 3)); v >>= 2; }
            c[3] = '\0';
            CHECK(wb_interest_add(&full, c) == 0, "the set fills");
        }
        CHECK(wb_interest_count(&full) == WB_INTEREST_MAX, "the set is full");
        CHECK(wb_interest_add(&full, "0000") == -1, "a full set refuses a new cell");
    }

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

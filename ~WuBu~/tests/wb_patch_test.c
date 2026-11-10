/*
 * wb_patch_test.c -- LP-48 stone 1 gate: the line patch applier.
 *
 * Devil's-advocate cases: hunks applied out of order in the caller's array still
 * land correctly (they are applied bottom-up), a hunk that runs past the end is
 * refused without writing anything, and a text with a trailing newline does not
 * gain a phantom final line.
 */
#include "wb_patch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static int lines_equal(const wb_patch_result *r, const char *const *expect, uint32_t n) {
    if (r->n != n) return 0;
    for (uint32_t i = 0; i < n; i++) if (strcmp(r->lines[i], expect[i]) != 0) return 0;
    return 1;
}

int main(void) {
    /* ---- split ---- */
    wb_patch_result r;
    CHECK(wb_patch_split("alpha\nbeta\ngamma\n", &r) == 0, "the text is split");
    CHECK(r.n == 3, "three lines are split out");
    const char *e3[3] = { "alpha", "beta", "gamma" };
    CHECK(lines_equal(&r, e3, 3), "the lines are correct");
    wb_patch_result_free(&r);

    /* a trailing newline does not create a phantom line */
    CHECK(wb_patch_split("one\n", &r) == 0 && r.n == 1, "a trailing newline adds no empty line");
    wb_patch_result_free(&r);

    /* an empty line in the middle is preserved */
    CHECK(wb_patch_split("a\n\nb\n", &r) == 0 && r.n == 3, "an empty line in the middle is kept");
    wb_patch_result_free(&r);

    CHECK(wb_patch_split("", &r) == 0 && r.n == 0, "an empty text splits to nothing");
    wb_patch_result_free(&r);

    CHECK(wb_patch_split("no newline", &r) == 0 && r.n == 1, "text without a newline is one line");
    wb_patch_result_free(&r);

    /* ---- a single hunk in the middle ---- */
    const char *add1[1] = { "BETA" };
    wb_patch_hunk h1 = { 1, 1, 1, add1 };
    CHECK(wb_patch_apply("alpha\nbeta\ngamma\n", &h1, 1, &r) == 0, "a middle hunk applies");
    const char *e_mid[3] = { "alpha", "BETA", "gamma" };
    CHECK(lines_equal(&r, e_mid, 3), "the middle line is replaced");
    wb_patch_result_free(&r);

    /* ---- an insertion (nremove = 0) ---- */
    const char *add2[2] = { "x", "y" };
    wb_patch_hunk h2 = { 1, 0, 2, add2 };
    CHECK(wb_patch_apply("alpha\nbeta\n", &h2, 1, &r) == 0, "an insertion applies");
    const char *e_ins[4] = { "alpha", "x", "y", "beta" };
    CHECK(lines_equal(&r, e_ins, 4), "the lines are inserted at the right place");
    wb_patch_result_free(&r);

    /* ---- a deletion (nadd = 0) ---- */
    wb_patch_hunk h3 = { 0, 1, 0, NULL };
    CHECK(wb_patch_apply("alpha\nbeta\ngamma\n", &h3, 1, &r) == 0, "a deletion applies");
    const char *e_del[2] = { "beta", "gamma" };
    CHECK(lines_equal(&r, e_del, 2), "the first line is deleted");
    wb_patch_result_free(&r);

    /* ---- several hunks, given out of order ---- */
    const char *addA[1] = { "A" };
    const char *addC[1] = { "C" };
    wb_patch_hunk hs[2] = { {2, 1, 1, addC}, {0, 1, 1, addA} };   /* C first, then A */
    CHECK(wb_patch_apply("alpha\nbeta\ngamma\n", hs, 2, &r) == 0, "two hunks apply");
    const char *e_two[3] = { "A", "beta", "C" };
    CHECK(lines_equal(&r, e_two, 3), "both hunks landed despite the array order");
    wb_patch_result_free(&r);

    /* ---- a hunk past the end is refused ---- */
    wb_patch_hunk bad1 = { 3, 1, 1, add1 };
    CHECK(wb_patch_apply("alpha\nbeta\ngamma\n", &bad1, 1, &r) == -1, "a hunk past the end is refused");
    CHECK(r.lines == NULL && r.n == 0, "a refused patch writes nothing");

    /* ---- a hunk removing past the end is refused ---- */
    wb_patch_hunk bad2 = { 2, 5, 0, NULL };
    CHECK(wb_patch_apply("alpha\nbeta\ngamma\n", &bad2, 1, &r) == -1, "a removal past the end is refused");

    /* ---- a bad hunk after a good one leaves nothing behind ---- */
    wb_patch_hunk mixed[2] = { {0, 1, 1, add1}, {9, 1, 1, add1} };
    CHECK(wb_patch_apply("alpha\nbeta\n", mixed, 2, &r) == -1, "a bad hunk fails the whole patch");
    CHECK(r.lines == NULL && r.n == 0, "nothing is left after a failed patch");

    /* ---- join ---- */
    CHECK(wb_patch_apply("alpha\nbeta\n", &h1, 1, &r) == 0, "a patch applies for joining");
    char *joined = wb_patch_join(&r);
    CHECK(joined && strcmp(joined, "alpha\nBETA\n") == 0, "the lines join back with newlines");
    free(joined);
    wb_patch_result_free(&r);

    /* ---- join an empty result ---- */
    wb_patch_result empty;
    CHECK(wb_patch_split("", &empty) == 0, "an empty split succeeds");
    joined = wb_patch_join(&empty);
    CHECK(joined && strcmp(joined, "") == 0, "an empty result joins to an empty string");
    free(joined);
    wb_patch_result_free(&empty);

    /* ---- determinism ---- */
    wb_patch_result a, b;
    CHECK(wb_patch_apply("alpha\nbeta\ngamma\n", hs, 2, &a) == 0, "the first apply succeeds");
    CHECK(wb_patch_apply("alpha\nbeta\ngamma\n", hs, 2, &b) == 0, "the second apply succeeds");
    CHECK(a.n == b.n && lines_equal(&a, (const char *const *)b.lines, b.n), "applying is deterministic");
    wb_patch_result_free(&a);
    wb_patch_result_free(&b);

    /* ---- degenerate input ---- */
    CHECK(wb_patch_split(NULL, &r) == -1, "a NULL text is refused");
    CHECK(wb_patch_split("x", NULL) == -1, "a NULL result is refused");
    CHECK(wb_patch_apply(NULL, &h1, 1, &r) == -1, "a NULL text is refused by apply");
    CHECK(wb_patch_apply("alpha\n", &h1, 1, NULL) == -1, "a NULL result is refused by apply");
    CHECK(wb_patch_apply("alpha\n", NULL, 0, &r) == 0 && r.n == 1, "zero hunks is a no-op");
    wb_patch_result_free(&r);
    CHECK(wb_patch_join(NULL) == NULL, "joining NULL returns NULL");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

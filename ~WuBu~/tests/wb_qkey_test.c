/*
 * wb_qkey_test.c -- LP-108 stone 1 gate: quadkey spatial queries.
 *
 * Devil's-advocate cases: adjacency is only defined WITHIN a level, so a cell and
 * its own child are not neighbours; a diagonal cell is not an edge neighbour; two
 * cells at opposite edges of the world are not neighbours either; the root has no
 * neighbours and no parent; and enclosing a cell with its own ancestor returns
 * the ancestor, not something deeper.
 */
#include "wb_qkey.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

int main(void) {
    char q[WB_QKEY_MAX];

    /* ---- the digit mapping: digit = 2*row_bit + col_bit, MSB first ---- */
    CHECK(wb_qkey_encode(1, 0, 0, q, sizeof q) == 0 && strcmp(q, "0") == 0, "(0,0) at level 1 is \"0\"");
    CHECK(wb_qkey_encode(1, 1, 0, q, sizeof q) == 0 && strcmp(q, "1") == 0, "(1,0) at level 1 is \"1\"");
    CHECK(wb_qkey_encode(1, 0, 1, q, sizeof q) == 0 && strcmp(q, "2") == 0, "(0,1) at level 1 is \"2\"");
    CHECK(wb_qkey_encode(1, 1, 1, q, sizeof q) == 0 && strcmp(q, "3") == 0, "(1,1) at level 1 is \"3\"");
    CHECK(wb_qkey_encode(2, 0, 0, q, sizeof q) == 0 && strcmp(q, "00") == 0, "(0,0) at level 2 is \"00\"");
    CHECK(wb_qkey_encode(2, 1, 0, q, sizeof q) == 0 && strcmp(q, "01") == 0, "(1,0) at level 2 is \"01\"");
    CHECK(wb_qkey_encode(2, 0, 1, q, sizeof q) == 0 && strcmp(q, "02") == 0, "(0,1) at level 2 is \"02\"");
    CHECK(wb_qkey_encode(2, 3, 3, q, sizeof q) == 0 && strcmp(q, "33") == 0, "(3,3) at level 2 is \"33\"");
    CHECK(wb_qkey_encode(0, 0, 0, q, sizeof q) == 0 && strcmp(q, "") == 0, "level 0 is the root \"\"");

    /* ---- round trips ---- */
    for (uint32_t level = 1; level <= 10; level++) {
        uint32_t max = 1u << level;
        int ok = 1;
        for (uint32_t col = 0; col < max && ok; col++) {
            for (uint32_t row = 0; row < max && ok; row++) {
                char enc[WB_QKEY_MAX];
                if (wb_qkey_encode(level, col, row, enc, sizeof enc) != 0) { ok = 0; break; }
                uint32_t dl = 0, dc = 0, dr = 0;
                if (wb_qkey_decode(enc, &dl, &dc, &dr) != 0) { ok = 0; break; }
                if (dl != level || dc != col || dr != row) ok = 0;
            }
        }
        CHECK(ok, "every cell at this level round-trips");
    }
    {
        uint32_t l, c, r;
        CHECK(wb_qkey_decode("0123", &l, &c, &r) == 0 && l == 4 && c == 5 && r == 3,
              "a level-4 key decodes to its coordinates");
        char back[WB_QKEY_MAX];
        CHECK(wb_qkey_encode(l, c, r, back, sizeof back) == 0 && strcmp(back, "0123") == 0,
              "and encodes back to the same key");
    }

    /* ---- invalid keys and coordinates ---- */
    CHECK(wb_qkey_encode(2, 4, 0, q, sizeof q) == -1, "a column past the level is refused");
    CHECK(wb_qkey_encode(2, 0, 4, q, sizeof q) == -1, "a row past the level is refused");
    CHECK(wb_qkey_encode(1, 2, 0, q, sizeof q) == -1, "a level-1 column of 2 is refused");
    CHECK(wb_qkey_encode(WB_QKEY_MAX, 0, 0, q, sizeof q) == -1, "an over-long level is refused");
    CHECK(wb_qkey_encode(2, 0, 0, q, 1) == -1, "a one-byte buffer is refused");
    CHECK(wb_qkey_encode(2, 0, 0, NULL, 10) == -1, "a NULL output is refused");
    CHECK(wb_qkey_encode(2, 0, 0, q, 0) == -1, "a zero capacity is refused");
    CHECK(wb_qkey_decode("45", NULL, NULL, NULL) == -1, "an invalid digit is refused");
    CHECK(wb_qkey_decode("00x1", NULL, NULL, NULL) == -1, "a non-digit is refused");
    CHECK(wb_qkey_decode(NULL, NULL, NULL, NULL) == -1, "a NULL key is refused");
    {
        char big[WB_QKEY_MAX + 1];
        memset(big, '0', WB_QKEY_MAX);
        big[WB_QKEY_MAX] = '\0';
        CHECK(wb_qkey_decode(big, NULL, NULL, NULL) == -1, "an over-long key is refused");
    }
    CHECK(wb_qkey_valid("") == 1, "the empty key is valid (the root)");
    CHECK(wb_qkey_valid("0123") == 1, "a normal key is valid");
    CHECK(wb_qkey_valid("0124") == 0, "a digit above 3 is invalid");
    CHECK(wb_qkey_valid(NULL) == 0, "a NULL key is invalid");
    CHECK(wb_qkey_depth("") == 0, "the root is level 0");
    CHECK(wb_qkey_depth("0123") == 4, "the level is the length");

    /* ---- containment ---- */
    CHECK(wb_qkey_contains("", "0123") == 1, "the root contains everything");
    CHECK(wb_qkey_contains("0", "0123") == 1, "a prefix contains its extension");
    CHECK(wb_qkey_contains("01", "0123") == 1, "a two-level prefix contains");
    CHECK(wb_qkey_contains("0123", "0123") == 1, "a cell contains itself");
    CHECK(wb_qkey_contains("0123", "01") == 0, "a child does not contain its parent");
    CHECK(wb_qkey_contains("01", "02") == 0, "a sibling does not contain a sibling");
    CHECK(wb_qkey_contains("1", "0") == 0, "an uncle does not contain a nephew");
    CHECK(wb_qkey_equal("01", "01") == 1, "equal keys are equal");
    CHECK(wb_qkey_equal("01", "02") == 0, "different keys are not equal");
    CHECK(wb_qkey_equal("0", "00") == 0, "a parent is not equal to its child");

    /* ---- overlap: disjoint, or one contains the other ---- */
    CHECK(wb_qkey_overlaps("01", "0123") == 1, "an ancestor overlaps its descendant");
    CHECK(wb_qkey_overlaps("0123", "01") == 1, "the overlap is symmetric");
    CHECK(wb_qkey_overlaps("01", "02") == 0, "siblings are disjoint");
    CHECK(wb_qkey_overlaps("0", "3") == 0, "half the world does not overlap the other half");
    CHECK(wb_qkey_overlaps("", "") == 1, "the root overlaps itself");

    /* ---- adjacency ---- */
    CHECK(wb_qkey_adjacent("00", "01") == 1, "a horizontal neighbour is adjacent");
    CHECK(wb_qkey_adjacent("01", "00") == 1, "adjacency is symmetric");
    CHECK(wb_qkey_adjacent("00", "02") == 1, "a vertical neighbour is adjacent");
    CHECK(wb_qkey_adjacent("01", "10") == 1, "a neighbour across a level-2 edge is adjacent");
    CHECK(wb_qkey_adjacent("00", "03") == 0, "a diagonal cell is NOT adjacent");
    CHECK(wb_qkey_adjacent("00", "0") == 0, "a cell and its own parent are not neighbours");
    CHECK(wb_qkey_adjacent("01", "010") == 0, "different levels are not neighbours");
    CHECK(wb_qkey_adjacent("00", "11") == 0, "opposite corners of the world are not neighbours");
    CHECK(wb_qkey_adjacent("00", "00") == 0, "a cell is not its own neighbour");
    CHECK(wb_qkey_adjacent("", "0") == 0, "the root has no neighbours");
    CHECK(wb_qkey_adjacent("0", "1") == 1, "the two halves of the world are adjacent");
    CHECK(wb_qkey_adjacent("0", "2") == 1, "the other pair of halves is adjacent");
    CHECK(wb_qkey_adjacent("0", "3") == 0, "a diagonal half is not adjacent");
    CHECK(wb_qkey_adjacent(NULL, "0") == 0, "a NULL key is not adjacent to anything");

    /* ---- enclosing ---- */
    char e[WB_QKEY_MAX];
    CHECK(wb_qkey_enclose("00", "01", e, sizeof e) == 0 && strcmp(e, "0") == 0, "siblings enclose to their parent");
    CHECK(wb_qkey_enclose("00", "02", e, sizeof e) == 0 && strcmp(e, "0") == 0, "vertical siblings enclose to their parent");
    CHECK(wb_qkey_enclose("00", "11", e, sizeof e) == 0 && strcmp(e, "") == 0, "opposite corners enclose to the root");
    CHECK(wb_qkey_enclose("0123", "0121", e, sizeof e) == 0 && strcmp(e, "012") == 0, "a deep common ancestor is found");
    CHECK(wb_qkey_enclose("0123", "0133", e, sizeof e) == 0 && strcmp(e, "01") == 0, "a shallow common ancestor is found");
    CHECK(wb_qkey_enclose("01", "01", e, sizeof e) == 0 && strcmp(e, "01") == 0, "a cell encloses with itself");
    CHECK(wb_qkey_enclose("0", "01", e, sizeof e) == 0 && strcmp(e, "0") == 0, "enclosing with an ancestor returns the ancestor");
    CHECK(wb_qkey_enclose("", "0123", e, sizeof e) == 0 && strcmp(e, "") == 0, "enclosing with the root returns the root");
    CHECK(wb_qkey_enclose("0123", "0", e, sizeof e) == 0 && strcmp(e, "0") == 0, "enclosing is symmetric");
    CHECK(wb_qkey_enclose("01", "02", e, 1) == -1, "a one-byte buffer is refused");
    CHECK(wb_qkey_enclose("01", "02", NULL, 10) == -1, "a NULL output is refused");
    CHECK(wb_qkey_enclose("01", "4", e, sizeof e) == -1, "an invalid key is refused");

    /* ---- parent ---- */
    char p[WB_QKEY_MAX];
    CHECK(wb_qkey_up("0", p, sizeof p) == 0 && strcmp(p, "") == 0, "the parent of a half is the root");
    CHECK(wb_qkey_up("01", p, sizeof p) == 0 && strcmp(p, "0") == 0, "a two-level parent");
    CHECK(wb_qkey_up("0123", p, sizeof p) == 0 && strcmp(p, "012") == 0, "a four-level parent");
    CHECK(wb_qkey_up("", p, sizeof p) == -1, "the root has no parent");
    CHECK(wb_qkey_up("01", p, 1) == -1, "a one-byte buffer is refused");
    CHECK(wb_qkey_up("01", NULL, 10) == -1, "a NULL output is refused");
    CHECK(wb_qkey_up(NULL, p, sizeof p) == -1, "a NULL key is refused");

    /* ---- determinism ---- */
    char d1[WB_QKEY_MAX], d2[WB_QKEY_MAX];
    CHECK(wb_qkey_enclose("0123", "0133", d1, sizeof d1) == 0, "the first enclose succeeds");
    CHECK(wb_qkey_enclose("0123", "0133", d2, sizeof d2) == 0, "the second enclose succeeds");
    CHECK(strcmp(d1, d2) == 0, "enclosing is deterministic");
    CHECK(wb_qkey_contains("01", "0123") == wb_qkey_contains("01", "0123"), "containment is deterministic");
    CHECK(wb_qkey_adjacent("00", "01") == wb_qkey_adjacent("00", "01"), "adjacency is deterministic");

    /* ---- degenerate input ---- */
    CHECK(wb_qkey_contains(NULL, "0") == 0, "a NULL container contains nothing");
    CHECK(wb_qkey_contains("0", NULL) == 0, "a NULL contained cell is contained nowhere");
    CHECK(wb_qkey_overlaps(NULL, "0") == 0, "a NULL key overlaps nothing");
    CHECK(wb_qkey_equal(NULL, "0") == 0, "a NULL key equals nothing");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

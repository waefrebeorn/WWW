/*
 * wb_addr_test.c -- LP-104 stone 1 gate: hierarchical place addresses.
 *
 * Devil's-advocate cases: parsing canonicalizes CASE, so "Earth/Europe" and
 * "earth/europe" are the same place; an empty segment, a leading or trailing
 * separator, a space, a dot and a colon are all refused; too many levels and a
 * too-long segment are refused; and is_ancestor is a PROPER prefix, so a place is
 * not its own ancestor.
 */
#include "wb_addr.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static int fmt_ok(const char *in, const char *want) {
    wb_addr a;
    char out[WB_ADDR_MAX];
    if (wb_addr_parse(in, &a) != 0) return 0;
    if (wb_addr_format(&a, out, sizeof out) != 0) return 0;
    return strcmp(out, want) == 0;
}

int main(void) {
    char out[WB_ADDR_MAX];

    /* ---- parse and format ---- */
    wb_addr a;
    CHECK(wb_addr_parse("earth", &a) == 0, "a single level parses");
    CHECK(wb_addr_depth(&a) == 1, "the depth is one");
    CHECK(wb_addr_format(&a, out, sizeof out) == 0 && strcmp(out, "earth") == 0, "and formats back");
    CHECK(wb_addr_parse("earth/europe/helsinki", &a) == 0, "three levels parse");
    CHECK(wb_addr_depth(&a) == 3, "the depth is three");
    CHECK(wb_addr_format(&a, out, sizeof out) == 0 && strcmp(out, "earth/europe/helsinki") == 0,
          "and formats back");
    CHECK(wb_addr_parse("universe/world/continent/city/patch/stadium", &a) == 0, "six levels parse");
    CHECK(wb_addr_depth(&a) == 6, "the depth is six");

    /* ---- canonicalization ---- */
    CHECK(fmt_ok("Earth/Europe", "earth/europe"), "parsing lowercases");
    CHECK(fmt_ok("EARTH", "earth"), "an all-caps name lowercases");
    CHECK(fmt_ok("Earth/EUROPE/Helsinki", "earth/europe/helsinki"), "every segment lowercases");
    CHECK(fmt_ok("a_b-c/d_e-f", "a_b-c/d_e-f"), "underscores and hyphens survive");
    CHECK(fmt_ok("world1/area2", "world1/area2"), "digits survive");
    CHECK(wb_addr_parse("Earth/Europe", &a) == 0 && wb_addr_parse("earth/europe", &a) == 0,
          "both cases parse");
    CHECK(wb_addr_equal(&a, &a), "an address equals itself");

    /* ---- malformed input ---- */
    CHECK(wb_addr_parse("", &a) == -1, "an empty address is refused");
    CHECK(wb_addr_parse(NULL, &a) == -1, "a NULL address is refused");
    CHECK(wb_addr_parse("/earth", &a) == -1, "a leading separator is refused");
    CHECK(wb_addr_parse("earth/", &a) == -1, "a trailing separator is refused");
    CHECK(wb_addr_parse("earth//europe", &a) == -1, "an empty segment is refused");
    CHECK(wb_addr_parse("earth/eu rope", &a) == -1, "a space is refused");
    CHECK(wb_addr_parse("earth/eu.rope", &a) == -1, "a dot is refused");
    CHECK(wb_addr_parse("earth:europe", &a) == -1, "a colon is refused (the separator is /)");
    CHECK(wb_addr_parse("earth/europe#1", &a) == -1, "a hash is refused");
    CHECK(wb_addr_parse("earth/europe?x=1", &a) == -1, "a query string is refused");
    CHECK(wb_addr_parse("earth/europe\t", &a) == -1, "a tab is refused");
    {
        char deep[WB_ADDR_MAX];
        strcpy(deep, "a/b/c/d/e/f/g/h");                  /* eight levels */
        CHECK(wb_addr_parse(deep, &a) == 0 && wb_addr_depth(&a) == 8, "eight levels are allowed");
        strcat(deep, "/i");                               /* nine levels */
        CHECK(wb_addr_parse(deep, &a) == -1, "nine levels are refused");
    }
    {
        char longseg[WB_ADDR_SEG_MAX + 8];
        memset(longseg, 'a', WB_ADDR_SEG_MAX);            /* 32 chars, no room for a NUL */
        longseg[WB_ADDR_SEG_MAX] = '\0';
        CHECK(wb_addr_parse(longseg, &a) == -1, "a segment at the limit is refused");
        longseg[WB_ADDR_SEG_MAX - 1] = '\0';
        CHECK(wb_addr_parse(longseg, &a) == 0, "a segment one under the limit is allowed");
    }
    CHECK(wb_addr_parse("earth", NULL) == -1, "a NULL address struct is refused");

    /* ---- equality ---- */
    {
        wb_addr x, y;
        wb_addr_parse("earth/europe", &x);
        wb_addr_parse("earth/europe", &y);
        CHECK(wb_addr_equal(&x, &y) == 1, "the same path is equal");
        wb_addr_parse("earth/asia", &y);
        CHECK(wb_addr_equal(&x, &y) == 0, "a different path is not equal");
        wb_addr_parse("earth", &y);
        CHECK(wb_addr_equal(&x, &y) == 0, "a prefix is not equal");
        CHECK(wb_addr_equal(NULL, &x) == 0, "a NULL address equals nothing");
        CHECK(wb_addr_equal(&x, NULL) == 0, "nothing equals a NULL address");
    }

    /* ---- containment ---- */
    {
        wb_addr earth, eu, hel, asia;
        wb_addr_parse("earth", &earth);
        wb_addr_parse("earth/europe", &eu);
        wb_addr_parse("earth/europe/helsinki", &hel);
        wb_addr_parse("earth/asia", &asia);
        CHECK(wb_addr_contains(&earth, &hel) == 1, "earth contains helsinki");
        CHECK(wb_addr_contains(&eu, &hel) == 1, "europe contains helsinki");
        CHECK(wb_addr_contains(&hel, &hel) == 1, "a place contains itself");
        CHECK(wb_addr_contains(&hel, &earth) == 0, "a child does not contain its parent");
        CHECK(wb_addr_contains(&eu, &asia) == 0, "a sibling does not contain a sibling");
        CHECK(wb_addr_contains(&earth, &asia) == 1, "a parent contains every child");
        CHECK(wb_addr_is_ancestor(&earth, &hel) == 1, "earth is a proper ancestor of helsinki");
        CHECK(wb_addr_is_ancestor(&eu, &hel) == 1, "europe is a proper ancestor of helsinki");
        CHECK(wb_addr_is_ancestor(&hel, &hel) == 0, "a place is NOT its own ancestor");
        CHECK(wb_addr_is_ancestor(&hel, &earth) == 0, "a child is not an ancestor of its parent");
        CHECK(wb_addr_is_ancestor(&eu, &asia) == 0, "a sibling is not an ancestor");
    }

    /* ---- common prefix ---- */
    {
        wb_addr x, y, c;
        wb_addr_parse("earth/europe/helsinki", &x);
        wb_addr_parse("earth/europe/oslo", &y);
        CHECK(wb_addr_common(&x, &y, &c) == 0 && wb_addr_depth(&c) == 2, "a common prefix of two cities");
        CHECK(wb_addr_format(&c, out, sizeof out) == 0 && strcmp(out, "earth/europe") == 0,
              "and it formats as the shared levels");
        wb_addr_parse("earth/asia/tokyo", &y);
        CHECK(wb_addr_common(&x, &y, &c) == 0 && wb_addr_depth(&c) == 1, "a shallow common prefix");
        CHECK(wb_addr_format(&c, out, sizeof out) == 0 && strcmp(out, "earth") == 0, "and it formats");
        wb_addr_parse("earth/europe/helsinki", &y);
        CHECK(wb_addr_common(&x, &y, &c) == 0 && wb_addr_depth(&c) == 3, "a place with itself is itself");
        wb_addr_parse("mars", &y);
        CHECK(wb_addr_common(&x, &y, &c) == 0 && wb_addr_depth(&c) == 0, "unrelated places share nothing");
        CHECK(wb_addr_format(&c, out, sizeof out) == 0 && strcmp(out, "") == 0, "and the empty common prefix formats");
        CHECK(wb_addr_common(&x, &y, NULL) == -1, "a NULL output is refused");
    }

    /* ---- segments ---- */
    {
        wb_addr x;
        wb_addr_parse("earth/europe/helsinki", &x);
        CHECK(wb_addr_segment(&x, 0, out, sizeof out) == 0 && strcmp(out, "earth") == 0, "segment 0");
        CHECK(wb_addr_segment(&x, 1, out, sizeof out) == 0 && strcmp(out, "europe") == 0, "segment 1");
        CHECK(wb_addr_segment(&x, 2, out, sizeof out) == 0 && strcmp(out, "helsinki") == 0, "segment 2");
        CHECK(wb_addr_segment(&x, 3, out, sizeof out) == -1, "a past-the-end segment is refused");
        CHECK(wb_addr_segment(&x, 0, out, 3) == -1, "a short buffer is refused");
        CHECK(wb_addr_segment(&x, 0, NULL, 10) == -1, "a NULL output is refused");
        CHECK(wb_addr_segment(NULL, 0, out, sizeof out) == -1, "a NULL address is refused");
    }

    /* ---- validity ---- */
    CHECK(wb_addr_valid("earth") == 1, "a simple address is valid");
    CHECK(wb_addr_valid("earth/europe/helsinki") == 1, "a deep address is valid");
    CHECK(wb_addr_valid("Earth/Europe") == 1, "mixed case is valid");
    CHECK(wb_addr_valid("earth/") == 0, "a trailing separator is invalid");
    CHECK(wb_addr_valid("earth//europe") == 0, "an empty segment is invalid");
    CHECK(wb_addr_valid("earth europe") == 0, "a space is invalid");
    CHECK(wb_addr_valid("") == 0, "an empty string is invalid");
    CHECK(wb_addr_valid(NULL) == 0, "a NULL string is invalid");

    /* ---- determinism ---- */
    {
        wb_addr x1, x2;
        char f1[WB_ADDR_MAX], f2[WB_ADDR_MAX];
        wb_addr_parse("Earth/Europe/Helsinki", &x1);
        wb_addr_parse("earth/europe/helsinki", &x2);
        wb_addr_format(&x1, f1, sizeof f1);
        wb_addr_format(&x2, f2, sizeof f2);
        CHECK(strcmp(f1, f2) == 0, "formatting is deterministic across case");
        CHECK(wb_addr_equal(&x1, &x2) == 1, "case alone does not distinguish two places");
        wb_addr_parse("earth/europe/helsinki", &x1);
        wb_addr_parse("earth/europe/oslo", &x2);
        CHECK(wb_addr_common(&x1, &x2, &a) == 0 && wb_addr_common(&x1, &x2, &x2) == 0,
              "the common prefix is deterministic");
    }

    /* ---- degenerate input ---- */
    CHECK(wb_addr_depth(NULL) == 0, "a NULL address has no depth");
    CHECK(wb_addr_format(NULL, out, sizeof out) == -1, "a NULL address cannot be formatted");
    CHECK(wb_addr_format(&a, NULL, sizeof out) == -1, "a NULL output is refused");
    CHECK(wb_addr_format(&a, out, 0) == -1, "a zero capacity is refused");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

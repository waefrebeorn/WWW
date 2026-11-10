/*
 * wb_moderate_test.c -- LP-53 stone 1 gate: the moderation policy engine.
 *
 * Devil's-advocate cases: an empty pattern matches NOTHING (a rule with no
 * pattern is a configuration error, not a wildcard); the highest severity wins
 * regardless of rule order; a tie goes to the earlier rule; an ALLOW rule can
 * match and still allow; and whitespace collapse is what lets "  Bad   Word "
 * match the pattern "bad word".
 */
#include "wb_moderate.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static const wb_moderate_rule rules[] = {
    { "spam",      WB_MODERATE_FLAG,  1, "spam" },
    { "slur",      WB_MODERATE_BLOCK, 5, "slur" },
    { "worse",     WB_MODERATE_BLOCK, 9, "worse" },
    { "promo",     WB_MODERATE_ALLOW, 2, "promo" },
    { "",          WB_MODERATE_BLOCK, 99, "empty-pattern" },   /* matches nothing */
    { "bad word",  WB_MODERATE_BLOCK, 3, "bad word" },
    { "tie",       WB_MODERATE_FLAG,  7, "tie-earlier" },
    { "tie",       WB_MODERATE_BLOCK, 7, "tie-later" },
    { "deals",     WB_MODERATE_FLAG,  4, "deals" },
};
#define NRULES (uint32_t)(sizeof rules / sizeof rules[0])

static wb_moderate_policy pol;
static wb_moderate_policy empty_pol;

int main(void) {
    pol.rules = rules;
    pol.n = NRULES;
    empty_pol.rules = NULL;
    empty_pol.n = 0;

    char buf[256];

    /* ---- normalisation ---- */
    CHECK(wb_moderate_normalise("Hello World", buf, sizeof buf) == 11, "normalise lowercases");
    CHECK(strcmp(buf, "hello world") == 0, "the normalised text is lowercase");
    CHECK(wb_moderate_normalise("  a\t\tb\nc  ", buf, sizeof buf) == 5, "whitespace runs collapse");
    CHECK(strcmp(buf, "a b c") == 0, "tabs and newlines become single spaces");
    CHECK(wb_moderate_normalise("", buf, sizeof buf) == 0 && buf[0] == '\0', "empty input normalises to empty");
    CHECK(wb_moderate_normalise("   \t\n ", buf, sizeof buf) == 0 && buf[0] == '\0', "whitespace-only normalises to empty");
    CHECK(wb_moderate_normalise(NULL, buf, sizeof buf) == 0 && buf[0] == '\0', "NULL input is safe");
    CHECK(wb_moderate_normalise("abc", buf, 1) == 0 && buf[0] == '\0', "a one-byte buffer yields an empty string");
    CHECK(wb_moderate_normalise("abcdef", buf, 4) == 3 && strcmp(buf, "abc") == 0, "a short buffer truncates without overflowing");
    CHECK(wb_moderate_normalise("abc", NULL, 10) == 0, "a NULL output is safe");
    CHECK(wb_moderate_normalise("MiXeD", buf, sizeof buf) == 5 && strcmp(buf, "mixed") == 0, "mixed case lowercases");

    /* ---- no match allows ---- */
    int idx = -2;
    CHECK(wb_moderate_apply(&pol, "completely fine", &idx) == WB_MODERATE_ALLOW, "no match allows");
    CHECK(idx == -1, "no match reports no rule");
    CHECK(wb_moderate_matches(&pol, "completely fine") == 0, "matches() is false with no match");

    /* ---- a single match ---- */
    CHECK(wb_moderate_apply(&pol, "buy spam now", &idx) == WB_MODERATE_FLAG, "a flag rule flags");
    CHECK(idx == 0, "the audit trail names the rule");
    CHECK(wb_moderate_apply(&pol, "you slur", &idx) == WB_MODERATE_BLOCK, "a block rule blocks");
    CHECK(idx == 1, "the block rule is named");
    CHECK(wb_moderate_apply(&pol, "that is worse", &idx) == WB_MODERATE_BLOCK, "the worst rule blocks");
    CHECK(idx == 2, "the highest-severity rule is named");

    /* ---- the highest severity wins, whatever the order ---- */
    CHECK(wb_moderate_apply(&pol, "slur and worse", &idx) == WB_MODERATE_BLOCK && idx == 2,
          "severity 9 beats severity 5");
    CHECK(wb_moderate_apply(&pol, "worse promo", &idx) == WB_MODERATE_BLOCK && idx == 2,
          "a block beats a matching allow rule");
    CHECK(wb_moderate_apply(&pol, "spam promo", &idx) == WB_MODERATE_ALLOW && idx == 3,
          "severity 2 (allow) outranks severity 1 (flag)");
    CHECK(wb_moderate_apply(&pol, "deals promo", &idx) == WB_MODERATE_FLAG && idx == 8,
          "severity 4 (flag) outranks severity 2 (allow)");

    /* ---- a tie goes to the earlier rule ---- */
    CHECK(wb_moderate_apply(&pol, "tie", &idx) == WB_MODERATE_FLAG && idx == 6,
          "a tie breaks to the earlier rule");

    /* ---- an ALLOW rule can match and still allow ---- */
    CHECK(wb_moderate_apply(&pol, "PROMO", &idx) == WB_MODERATE_ALLOW, "an allow rule allows");
    CHECK(idx == 3, "the allow rule is still named for the audit trail");
    CHECK(wb_moderate_matches(&pol, "PROMO") == 1, "matches() sees an allow rule");

    /* ---- matching is case-insensitive over collapsed whitespace ---- */
    CHECK(wb_moderate_apply(&pol, "  Bad   Word ", &idx) == WB_MODERATE_BLOCK && idx == 5,
          "whitespace collapse lets a phrase rule match");
    CHECK(wb_moderate_apply(&pol, "BAD\tWORD", &idx) == WB_MODERATE_BLOCK, "a tab inside the phrase matches");

    /* ---- an empty pattern matches nothing ---- */
    CHECK(wb_moderate_apply(&pol, "", &idx) == WB_MODERATE_ALLOW && idx == -1, "empty text matches nothing");
    CHECK(wb_moderate_apply(&pol, "empty-pattern", &idx) == WB_MODERATE_ALLOW && idx == -1,
          "the empty pattern never matches, even when named");

    /* ---- a NULL rule table allows ---- */
    CHECK(wb_moderate_apply(&pol, NULL, &idx) == WB_MODERATE_ALLOW && idx == -1, "NULL text allows");
    CHECK(wb_moderate_apply(NULL, "slur", &idx) == WB_MODERATE_ALLOW, "a NULL policy allows");
    CHECK(wb_moderate_apply(&empty_pol, "slur", &idx) == WB_MODERATE_ALLOW, "an empty policy allows");
    CHECK(wb_moderate_matches(NULL, "slur") == 0, "a NULL policy has no matches");

    /* ---- determinism ---- */
    int a_idx = -1, b_idx = -1;
    int a = wb_moderate_apply(&pol, "buy spam, what a slur, worse than promo", &a_idx);
    int b = wb_moderate_apply(&pol, "buy spam, what a slur, worse than promo", &b_idx);
    CHECK(a == b && a_idx == b_idx, "moderation is deterministic");
    CHECK(a == WB_MODERATE_BLOCK && a_idx == 2, "the highest severity in a mixed message wins");

    /* ---- degenerate input ---- */
    CHECK(wb_moderate_normalise("x", buf, 0) == 0, "a zero capacity is safe");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

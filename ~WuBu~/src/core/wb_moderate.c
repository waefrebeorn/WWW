/*
 * wb_moderate.c -- the moderation policy engine (LP-53 stone 1). See wb_moderate.h.
 */
#include "wb_moderate.h"

#include <stdlib.h>
#include <string.h>

static int wb_moderate_ci_eq(char a, char b) {
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    return a == b;
}

/* A case-insensitive substring test over normalised text. */
static int wb_moderate_contains(const char *hay, const char *needle) {
    if (!hay || !needle || !*needle) return 0;
    for (const char *h = hay; *h; h++) {
        const char *a = h, *b = needle;
        while (*a && *b && wb_moderate_ci_eq(*a, *b)) { a++; b++; }
        if (!*b) return 1;
    }
    return 0;
}

static int wb_moderate_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

size_t wb_moderate_normalise(const char *in, char *out, size_t cap) {
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (!in) return 0;
    size_t w = 0;
    int pending_space = 0;
    for (const char *p = in; *p; p++) {
        if (wb_moderate_is_space(*p)) { pending_space = (w > 0); continue; }
        if (pending_space) {
            if (w + 1 >= cap) break;
            out[w++] = ' ';
            pending_space = 0;
        }
        if (w + 1 >= cap) break;
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        out[w++] = c;
    }
    out[w] = '\0';
    return w;
}

static int wb_moderate_rule_matches(const wb_moderate_rule *r, const char *normalised) {
    if (!r || !r->pattern || !*r->pattern) return 0;   /* an empty pattern is an error, not a wildcard */
    return wb_moderate_contains(normalised, r->pattern);
}

int wb_moderate_apply(const wb_moderate_policy *p, const char *text, int *rule_index) {
    if (rule_index) *rule_index = -1;
    if (!p || !p->rules) return WB_MODERATE_ALLOW;

    char buf[1024];
    size_t n = wb_moderate_normalise(text, buf, sizeof buf);
    /* normalise returns 0 both for "no input" and for "everything was trimmed";
     * either way there is nothing to match */
    if (n == 0) return WB_MODERATE_ALLOW;

    uint32_t best_sev = 0;
    int best_action = WB_MODERATE_ALLOW;
    int best_idx = -1;
    for (uint32_t i = 0; i < p->n; i++) {
        if (!wb_moderate_rule_matches(&p->rules[i], buf)) continue;
        uint32_t sev = p->rules[i].severity;
        /* strictly greater: a tie keeps the earlier rule, so the result does not
         * depend on how the table happens to be sorted */
        if (best_idx < 0 || sev > best_sev) {
            best_sev = sev;
            best_action = (int)p->rules[i].action;
            best_idx = (int)i;
        }
    }
    if (best_idx < 0) return WB_MODERATE_ALLOW;
    if (rule_index) *rule_index = best_idx;
    return best_action;
}

int wb_moderate_matches(const wb_moderate_policy *p, const char *text) {
    char buf[1024];
    if (wb_moderate_normalise(text, buf, sizeof buf) == 0) return 0;
    if (!p || !p->rules) return 0;
    for (uint32_t i = 0; i < p->n; i++)
        if (wb_moderate_rule_matches(&p->rules[i], buf)) return 1;
    return 0;
}

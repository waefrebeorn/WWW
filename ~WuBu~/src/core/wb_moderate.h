/*
 * wb_moderate.h -- the moderation policy engine (LP-53 stone 1).
 *
 * Moderation is a policy table and one question: given this text, what happens
 * to it. A rule is a case-insensitive substring pattern, an action (allow, flag
 * for review, block) and a severity. The HIGHEST-severity matching rule wins, so
 * a block rule always outranks a flag rule and a flag rule outranks silence,
 * whatever order the rules were loaded in.
 *
 * Text is normalised before matching -- lowercased, whitespace runs collapsed,
 * ends trimmed -- so "  Bad   Word " matches the pattern "bad word". An empty
 * pattern matches nothing: a rule with no pattern is a configuration error, not
 * a wildcard, and silently matching everything is how a policy quietly blocks a
 * whole server.
 *
 * C11, no deps. Deterministic: ties break to the lowest rule index.
 */
#ifndef WB_MODERATE_H
#define WB_MODERATE_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    WB_MODERATE_ALLOW = 0,
    WB_MODERATE_FLAG  = 1,   /* hold for human review */
    WB_MODERATE_BLOCK = 2    /* reject outright */
} wb_moderate_action;

typedef struct {
    const char *pattern;     /* a case-insensitive substring; NULL/empty matches nothing */
    uint32_t    action;      /* a wb_moderate_action */
    uint32_t    severity;    /* the highest matching severity wins */
    const char *label;       /* for the audit trail */
} wb_moderate_rule;

typedef struct {
    const wb_moderate_rule *rules;
    uint32_t                n;
} wb_moderate_policy;

/* Normalise for matching: lowercase, collapse whitespace runs to one space, trim
 * both ends. Returns the output length, or 0 on a NULL input. */
size_t wb_moderate_normalise(const char *in, char *out, size_t cap);

/* Apply the policy to `text`. The highest-severity matching rule decides the
 * action; ties break to the lowest rule index. Returns WB_MODERATE_ALLOW when
 * nothing matches. `rule_index`, if non-NULL, receives the winning rule's index
 * or -1 when nothing matched -- that is the audit trail. */
int wb_moderate_apply(const wb_moderate_policy *p, const char *text, int *rule_index);

/* 1 if any rule matches the text, regardless of its action. */
int wb_moderate_matches(const wb_moderate_policy *p, const char *text);

#endif /* WB_MODERATE_H */

/*
 * wb_match.h -- matchmaking team balance (LP-34 stone 1).
 *
 * The simplest fair split: sort players by skill, then deal each one to the
 * team with the lower total. It is the classic greedy balance -- not optimal,
 * but deterministic, O(n log n), and within one player's skill of optimal on
 * realistic distributions. Ties go to team 0, which keeps the result stable for
 * a given input order.
 *
 * C11, no deps.
 */
#ifndef WB_MATCH_H
#define WB_MATCH_H

#include <stdint.h>

/* Split `n` players into two teams. `skill[i]` is player i's rating; the team
 * assignment (0 or 1) is written to `out_team`. Returns the absolute difference
 * between the teams' total skill. */
uint32_t wb_match_balance(const uint32_t *skill, uint32_t n, uint8_t *out_team);

/* The total skill of a team after a balance. */
uint32_t wb_match_team_skill(const uint32_t *skill, const uint8_t *team, uint32_t n, uint32_t which);

#endif /* WB_MATCH_H */

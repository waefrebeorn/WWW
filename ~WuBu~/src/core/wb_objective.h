/*
 * wb_objective.h -- the match objective: score, clock, and a result.
 *
 * LP-114. Until now the shipped modes had no win condition at all: duel-stadium
 * stood still for 120 ticks and stopped, and omniverse-hub walked through a
 * gate. There was nothing to win, lose, or be scored on.
 *
 * This is the smallest honest version of an objective:
 *
 *   - a SCORE per side, which a mode's script or the host adds to;
 *   - a CLOCK, which advances only when the simulation advances -- never the
 *     wall clock, so a match replays identically;
 *   - a RESULT: won / lost / drawn, with a winner side or -1 for none.
 *
 * Three rules:
 *
 *   WB_OBJ_FIRST_TO  first side to `target` points wins, immediately, even if
 *                    the clock has not run out.
 *   WB_OBJ_TIMED     when the clock reaches the limit, the highest score wins;
 *                    level scores are a draw.
 *   WB_OBJ_SURVIVE   when the clock reaches the limit, sides with no deaths
 *                    win; exactly one survivor wins, more than one is a draw,
 *                    and all dead is a draw.
 *
 * The module is deterministic by construction: it is a pure function of its
 * state and the elapsed simulation time. No wall clock, no randomness, no
 * allocation.
 *
 * It also refuses rather than accepting nonsense: an unknown rule, a one-sided
 * objective, a zero target, a zero time limit on a timed rule, or a side index
 * outside the declared sides are all rejected or ignored rather than silently
 * accepted.
 */
#ifndef WB_OBJECTIVE_H
#define WB_OBJECTIVE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WB_OBJECTIVE_MAX_SIDES 8

/* The type names are deliberately short (wb_rule / wb_result) so the accessor
 * wb_objective_state() / wb_objective_rule() can read as what they return. */
typedef enum {
    WB_OBJ_FIRST_TO = 1,
    WB_OBJ_TIMED    = 2,
    WB_OBJ_SURVIVE  = 3
} wb_rule;

typedef enum {
    WB_OBJ_ONGOING = 0,
    WB_OBJ_WON     = 1,
    WB_OBJ_LOST    = 2,
    WB_OBJ_DRAW    = 3
} wb_result;

typedef struct {
    int      rule;
    int      state;
    int      winner;                                  /* -1 = none yet */
    uint32_t sides;
    uint32_t target;                                  /* FIRST_TO only */
    double   limit;                                   /* seconds; 0 = none */
    double   elapsed;
    uint32_t score[WB_OBJECTIVE_MAX_SIDES];
    uint32_t deaths[WB_OBJECTIVE_MAX_SIDES];
} wb_objective;

/* Start an objective. `limit_s` is the time limit in seconds (0 = none, which
 * only WB_OBJ_FIRST_TO accepts); `target` is the FIRST_TO score (ignored by the
 * other rules). Returns 0 on success, -1 on refusal -- `err` (if not NULL)
 * receives the reason and the objective is left zeroed. */
int wb_objective_init(wb_objective *o, int rule, uint32_t sides,
                      uint32_t target, double limit_s,
                      char *err, size_t err_sz);

/* Advance the clock by `dt` seconds of SIMULATION time and resolve any rule the
 * clock decides. Returns the state. A negative dt is treated as zero. Once the
 * objective is final it never changes again. */
int wb_objective_step(wb_objective *o, double dt);

/* Add `points` to `side` and resolve a FIRST_TO objective immediately. An
 * out-of-range side is ignored. Returns the state. */
int wb_objective_score(wb_objective *o, uint32_t side, uint32_t points);

/* Record a death for `side`. An out-of-range side is ignored. Returns the
 * state. */
int wb_objective_death(wb_objective *o, uint32_t side);

int       wb_objective_state(const wb_objective *o);
int       wb_objective_winner(const wb_objective *o);   /* -1 = none */
int       wb_objective_is_final(const wb_objective *o);
int       wb_objective_rule(const wb_objective *o);
uint32_t  wb_objective_sides(const wb_objective *o);
uint32_t  wb_objective_score_of(const wb_objective *o, uint32_t side);
uint32_t  wb_objective_deaths_of(const wb_objective *o, uint32_t side);
double    wb_objective_elapsed(const wb_objective *o);

/* Seconds left on the clock, never negative. A rule with no limit returns 0. */
double    wb_objective_clock(const wb_objective *o);

#ifdef __cplusplus
}
#endif

#endif /* WB_OBJECTIVE_H */

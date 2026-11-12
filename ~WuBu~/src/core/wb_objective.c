/*
 * wb_objective.c -- implementation. See wb_objective.h for the rules.
 *
 * Resolution is split deliberately: a FIRST_TO result is decided the moment the
 * point is scored, because a match is over the instant the target is reached;
 * the clock-decided rules are decided in step(), because they are over only
 * when the simulation says the time is up. Both paths then set the same
 * terminal state and never revisit it.
 */
#include "wb_objective.h"

#include <stdio.h>
#include <string.h>

/* The highest score held by exactly one side, and that side; -1 when the top
 * score is shared (a draw) or no side has scored. */
static int top_side(const wb_objective *o, uint32_t *out_score) {
    uint32_t best = 0;
    int      best_side = -1;
    int      tied = 0;
    for (uint32_t s = 0; s < o->sides; s++) {
        if (o->score[s] > best) {
            best = o->score[s];
            best_side = (int)s;
            tied = 0;
        } else if (o->score[s] == best && best > 0) {
            tied = 1;
        }
    }
    if (best == 0 || tied) best_side = -1;
    if (out_score) *out_score = best;
    return best_side;
}

int wb_objective_init(wb_objective *o, int rule, uint32_t sides,
                      uint32_t target, double limit_s,
                      char *err, size_t err_sz) {
    if (!o) return -1;
    memset(o, 0, sizeof *o);
    o->winner = -1;

    if (rule != WB_OBJ_FIRST_TO && rule != WB_OBJ_TIMED &&
        rule != WB_OBJ_SURVIVE) {
        if (err) snprintf(err, err_sz, "unknown objective rule %d", rule);
        return -1;
    }
    if (sides < 2 || sides > WB_OBJECTIVE_MAX_SIDES) {
        if (err) snprintf(err, err_sz,
                          "an objective needs 2 to %d sides, got %u",
                          WB_OBJECTIVE_MAX_SIDES, sides);
        return -1;
    }
    if (rule == WB_OBJ_FIRST_TO && target < 1) {
        if (err) snprintf(err, err_sz,
                          "first-to needs a target of at least 1");
        return -1;
    }
    if (rule != WB_OBJ_FIRST_TO && limit_s <= 0.0) {
        if (err) snprintf(err, err_sz,
                          "a timed objective needs a positive time limit");
        return -1;
    }

    o->rule   = rule;
    o->sides  = sides;
    o->target = target;
    o->limit  = limit_s;
    o->state  = WB_OBJ_ONGOING;
    return 0;
}

int wb_objective_step(wb_objective *o, double dt) {
    if (!o || o->state != WB_OBJ_ONGOING) return o ? o->state : WB_OBJ_ONGOING;
    if (dt > 0.0) o->elapsed += dt;
    if (o->limit <= 0.0 || o->elapsed < o->limit) return o->state;

    /* The clock is up. */
    if (o->rule == WB_OBJ_TIMED) {
        uint32_t best;
        int side = top_side(o, &best);
        if (side < 0) {
            o->state = WB_OBJ_DRAW;          /* level, or nobody scored */
            o->winner = -1;
        } else {
            o->state = WB_OBJ_WON;
            o->winner = side;
        }
        return o->state;
    }

    if (o->rule == WB_OBJ_SURVIVE) {
        int survivors = 0, last = -1;
        for (uint32_t s = 0; s < o->sides; s++) {
            if (o->deaths[s] == 0) { survivors++; last = (int)s; }
        }
        if (survivors == 1) {
            o->state = WB_OBJ_WON;
            o->winner = last;
        } else {
            o->state = WB_OBJ_DRAW;          /* one survivor is a win, else not */
            o->winner = -1;
        }
        return o->state;
    }

    /* FIRST_TO with a limit, and nobody reached the target. */
    o->state = WB_OBJ_DRAW;
    o->winner = -1;
    return o->state;
}

int wb_objective_score(wb_objective *o, uint32_t side, uint32_t points) {
    if (!o || o->state != WB_OBJ_ONGOING) return o ? o->state : WB_OBJ_ONGOING;
    if (side >= o->sides) return o->state;    /* out of range: ignored */
    if (points == 0) return o->state;

    o->score[side] += points;

    if (o->rule == WB_OBJ_FIRST_TO && o->score[side] >= o->target) {
        o->state = WB_OBJ_WON;
        o->winner = (int)side;
    }
    return o->state;
}

int wb_objective_death(wb_objective *o, uint32_t side) {
    if (!o || o->state != WB_OBJ_ONGOING) return o ? o->state : WB_OBJ_ONGOING;
    if (side >= o->sides) return o->state;
    o->deaths[side]++;
    return o->state;
}

int       wb_objective_state(const wb_objective *o)   { return o ? o->state : WB_OBJ_ONGOING; }
int       wb_objective_winner(const wb_objective *o)  { return o ? o->winner : -1; }
int       wb_objective_is_final(const wb_objective *o){ return o ? o->state != WB_OBJ_ONGOING : 1; }
int       wb_objective_rule(const wb_objective *o)    { return o ? o->rule : 0; }
uint32_t  wb_objective_sides(const wb_objective *o)   { return o ? o->sides : 0; }
uint32_t  wb_objective_score_of(const wb_objective *o, uint32_t side) {
    return (o && side < o->sides) ? o->score[side] : 0;
}
uint32_t  wb_objective_deaths_of(const wb_objective *o, uint32_t side) {
    return (o && side < o->sides) ? o->deaths[side] : 0;
}
double    wb_objective_elapsed(const wb_objective *o) { return o ? o->elapsed : 0.0; }

double wb_objective_clock(const wb_objective *o) {
    if (!o || o->limit <= 0.0) return 0.0;
    double left = o->limit - o->elapsed;
    return left > 0.0 ? left : 0.0;
}

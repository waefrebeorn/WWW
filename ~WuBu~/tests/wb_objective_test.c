/*
 * wb_objective_test.c -- verification of the match objective (LP-114).
 *
 * The point of this gate is not that the arithmetic works, it is that the
 * RESULT is right: a first-to resolves the instant the target is reached rather
 * than when the clock runs out, a timed match resolves only when the simulation
 * says the time is up, a level score is a draw, and a finished match never
 * changes its mind afterwards.
 */
#include "wb_objective.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) g_pass++; \
    else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } \
} while (0)

int main(void) {
    char err[256];

    /* ---- first-to: decided by the point, not the clock ---- */
    wb_objective o;
    CHECK(wb_objective_init(&o, WB_OBJ_FIRST_TO, 2, 3, 0.0, err, sizeof err) == 0,
          "first-to with no time limit is accepted");
    CHECK(wb_objective_state(&o) == WB_OBJ_ONGOING, "starts ongoing");
    CHECK(wb_objective_winner(&o) == -1, "no winner yet");
    CHECK(wb_objective_rule(&o) == WB_OBJ_FIRST_TO, "rule is recorded");
    CHECK(wb_objective_sides(&o) == 2, "sides are recorded");

    CHECK(wb_objective_score(&o, 0, 1) == WB_OBJ_ONGOING, "1 of 3 is not over");
    CHECK(wb_objective_score(&o, 1, 2) == WB_OBJ_ONGOING, "2 of 3 is not over");
    CHECK(wb_objective_score_of(&o, 0) == 1 && wb_objective_score_of(&o, 1) == 2,
          "scores accumulate per side");
    CHECK(wb_objective_score(&o, 1, 1) == WB_OBJ_WON, "reaching 3 wins at once");
    CHECK(wb_objective_winner(&o) == 1, "the winner is the side that scored");
    CHECK(wb_objective_is_final(&o), "a won match is final");
    CHECK(wb_objective_score(&o, 0, 5) == WB_OBJ_WON, "a finished match ignores more points");
    CHECK(wb_objective_score_of(&o, 0) == 1, "ignored points are not added");

    /* ---- first-to with a limit still resolves early ---- */
    wb_objective early;
    wb_objective_init(&early, WB_OBJ_FIRST_TO, 2, 2, 60.0, err, sizeof err);
    wb_objective_score(&early, 0, 1);
    wb_objective_step(&early, 59.0);
    CHECK(wb_objective_state(&early) == WB_OBJ_ONGOING,
          "a first-to with a limit stays ongoing until the target");
    wb_objective_score(&early, 0, 1);
    CHECK(wb_objective_state(&early) == WB_OBJ_WON && wb_objective_winner(&early) == 0,
          "first-to wins before the clock runs out");

    /* ---- timed: decided only by the clock ---- */
    wb_objective t;
    CHECK(wb_objective_init(&t, WB_OBJ_TIMED, 2, 1, 10.0, err, sizeof err) == 0,
          "a timed objective needs a limit and accepts one");
    wb_objective_score(&t, 0, 2);
    wb_objective_score(&t, 1, 5);
    wb_objective_step(&t, 9.9);
    CHECK(wb_objective_state(&t) == WB_OBJ_ONGOING,
          "a timed match is not over before the limit");
    CHECK(wb_objective_clock(&t) > 0.0, "the clock has time left");
    wb_objective_step(&t, 0.1);
    CHECK(wb_objective_state(&t) == WB_OBJ_WON, "the clock decides at the limit");
    CHECK(wb_objective_winner(&t) == 1, "the higher score wins");
    CHECK(wb_objective_clock(&t) == 0.0, "the clock stops at zero");

    /* level scores are a draw */
    wb_objective draw;
    wb_objective_init(&draw, WB_OBJ_TIMED, 2, 1, 1.0, err, sizeof err);
    wb_objective_score(&draw, 0, 3);
    wb_objective_score(&draw, 1, 3);
    wb_objective_step(&draw, 1.0);
    CHECK(wb_objective_state(&draw) == WB_OBJ_DRAW, "level scores are a draw");
    CHECK(wb_objective_winner(&draw) == -1, "a draw has no winner");

    /* nobody scored at all */
    wb_objective scoreless;
    wb_objective_init(&scoreless, WB_OBJ_TIMED, 3, 1, 1.0, err, sizeof err);
    wb_objective_step(&scoreless, 1.0);
    CHECK(wb_objective_state(&scoreless) == WB_OBJ_DRAW,
          "a scoreless timed match is a draw");

    /* a three-way race with a clear winner */
    wb_objective race;
    wb_objective_init(&race, WB_OBJ_TIMED, 3, 1, 1.0, err, sizeof err);
    wb_objective_score(&race, 2, 4);
    wb_objective_score(&race, 0, 1);
    wb_objective_score(&race, 1, 2);
    wb_objective_step(&race, 1.0);
    CHECK(wb_objective_state(&race) == WB_OBJ_WON && wb_objective_winner(&race) == 2,
          "the highest of three sides wins");

    /* ---- survive ---- */
    wb_objective sv;
    CHECK(wb_objective_init(&sv, WB_OBJ_SURVIVE, 2, 1, 30.0, err, sizeof err) == 0,
          "survive is accepted");
    wb_objective_death(&sv, 1);
    CHECK(wb_objective_deaths_of(&sv, 1) == 1, "a death is recorded");
    CHECK(wb_objective_state(&sv) == WB_OBJ_ONGOING,
          "survive does not end early on a death");
    wb_objective_step(&sv, 30.0);
    CHECK(wb_objective_state(&sv) == WB_OBJ_WON && wb_objective_winner(&sv) == 0,
          "the side with no deaths wins");

    wb_objective sv_draw;
    wb_objective_init(&sv_draw, WB_OBJ_SURVIVE, 3, 1, 1.0, err, sizeof err);
    wb_objective_step(&sv_draw, 1.0);
    CHECK(wb_objective_state(&sv_draw) == WB_OBJ_DRAW,
          "every side surviving is a draw");

    wb_objective sv_dead;
    wb_objective_init(&sv_dead, WB_OBJ_SURVIVE, 2, 1, 1.0, err, sizeof err);
    wb_objective_death(&sv_dead, 0);
    wb_objective_death(&sv_dead, 1);
    wb_objective_step(&sv_dead, 1.0);
    CHECK(wb_objective_state(&sv_dead) == WB_OBJ_DRAW,
          "every side dead is a draw");

    /* ---- the clock is the sim clock ---- */
    wb_objective idle;
    wb_objective_init(&idle, WB_OBJ_TIMED, 2, 1, 5.0, err, sizeof err);
    wb_objective_step(&idle, 0.0);
    CHECK(wb_objective_elapsed(&idle) == 0.0, "zero elapsed time advances nothing");
    wb_objective_step(&idle, -3.0);
    CHECK(wb_objective_elapsed(&idle) == 0.0, "negative time is ignored");
    wb_objective_step(&idle, 3.0);
    CHECK(wb_objective_elapsed(&idle) == 3.0, "elapsed accumulates");
    CHECK(wb_objective_clock(&idle) == 2.0, "the clock is the limit minus elapsed");

    /* Two identical matches run identically -- the whole point. */
    wb_objective a, b;
    wb_objective_init(&a, WB_OBJ_TIMED, 2, 1, 2.0, err, sizeof err);
    wb_objective_init(&b, WB_OBJ_TIMED, 2, 1, 2.0, err, sizeof err);
    for (int i = 0; i < 10; i++) {
        wb_objective_score(&a, i % 2, 1);
        wb_objective_score(&b, i % 2, 1);
        wb_objective_step(&a, 0.2);
        wb_objective_step(&b, 0.2);
    }
    CHECK(wb_objective_state(&a) == wb_objective_state(&b) &&
          wb_objective_score_of(&a, 0) == wb_objective_score_of(&b, 0) &&
          wb_objective_elapsed(&a) == wb_objective_elapsed(&b),
          "identical runs produce identical results");

    /* ---- refusals ---- */
    wb_objective bad;
    err[0] = 0;
    CHECK(wb_objective_init(&bad, 99, 2, 1, 0.0, err, sizeof err) == -1,
          "an unknown rule is refused");
    CHECK(err[0] != '\0', "the refusal says why");
    CHECK(wb_objective_state(&bad) == WB_OBJ_ONGOING, "a refused objective is inert");

    CHECK(wb_objective_init(&bad, WB_OBJ_FIRST_TO, 1, 1, 0.0, err, sizeof err) == -1,
          "a one-sided objective is refused");
    CHECK(wb_objective_init(&bad, WB_OBJ_FIRST_TO, 9, 1, 0.0, err, sizeof err) == -1,
          "too many sides is refused");
    CHECK(wb_objective_init(&bad, WB_OBJ_FIRST_TO, 2, 0, 0.0, err, sizeof err) == -1,
          "a zero target is refused");
    CHECK(wb_objective_init(&bad, WB_OBJ_TIMED, 2, 1, 0.0, err, sizeof err) == -1,
          "a timed objective with no limit is refused");
    CHECK(wb_objective_init(&bad, WB_OBJ_SURVIVE, 2, 1, -1.0, err, sizeof err) == -1,
          "a negative limit is refused");

    /* ---- out-of-range sides are ignored, not wrapped ---- */
    wb_objective r;
    wb_objective_init(&r, WB_OBJ_FIRST_TO, 2, 2, 0.0, err, sizeof err);
    CHECK(wb_objective_score(&r, 7, 5) == WB_OBJ_ONGOING,
          "an out-of-range side cannot score");
    CHECK(wb_objective_score_of(&r, 7) == 0, "an out-of-range side reads as zero");
    wb_objective_death(&r, 7);
    CHECK(wb_objective_deaths_of(&r, 7) == 0,
          "an out-of-range side cannot be killed");
    CHECK(wb_objective_score(&r, 0, 1) == WB_OBJ_ONGOING,
          "the objective still works after an out-of-range call");

    printf("=== OBJECTIVE TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

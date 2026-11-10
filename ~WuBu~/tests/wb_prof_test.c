/*
 * wb_prof_test.c -- LP-01 stone 1 gate: the frame profiler.
 *
 * Proves the instrumented machine actually measures:
 *   - pointer-to-accumulator counter: delta per frame, stable pointer identity
 *   - zone scopes time real work (and report 0 for frames where they don't run)
 *   - the ring folds to the last N frames with sane mean/min/max
 *   - nested (LIFO) zones each accumulate; null args are safe
 */
#include "wb_prof.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
#endif

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

/* fuzz a little so wb_prof_now() advances measurably */
static void busy(double secs) {
    volatile double acc = 0;
    double t0 = 0, now = 0;
#if defined(_WIN32)
    LARGE_INTEGER f, c1, c2;
    QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c1); QueryPerformanceCounter(&c2);
    t0 = (double)c1.QuadPart / (double)f.QuadPart; now = (double)c2.QuadPart / (double)f.QuadPart;
#else
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); t0 = ts.tv_sec + (double)ts.tv_nsec*1e-9;
#endif
    while (now - t0 < secs) {
        for (int i = 0; i < 1000; i++) acc += 1.0;
#if defined(_WIN32)
        QueryPerformanceCounter(&c2); now = (double)c2.QuadPart / (double)f.QuadPart;
#else
        clock_gettime(CLOCK_MONOTONIC, &ts); now = ts.tv_sec + (double)ts.tv_nsec*1e-9;
#endif
    }
    (void)acc;
}

int main(void) {
    /* ---- basic lifecycle + hot-path counter -------------------------- */
    wb_prof *p = wb_prof_new(8);
    CHECK(p != NULL, "profiler created (ring 8)");

    double *alive = wb_prof_counter(p, "alive");
    double *alive2 = wb_prof_counter(p, "alive");
    CHECK(alive != NULL && alive2 == alive, "counter pointer identity is stable");

    /* 3 frames, +1 each: running total 1,2,3 -> three deltas of 1 */
    for (int f = 0; f < 3; f++) {
        wb_prof_begin_frame(p);
        *alive += 1.0;
        wb_prof_end_frame(p);
    }
    wb_prof_stat st;
    CHECK(wb_prof_summary(p, "alive", &st) == 1, "stat found for alive");
    CHECK(st.n == 3, "three frames recorded");
    CHECK(st.last == 1.0f && st.mean == 1.0f, "per-frame delta is 1, mean 1");

    /* 3 more frames, no adds: untouched counters record 0 deltas */
    for (int f = 0; f < 3; f++) { wb_prof_begin_frame(p); wb_prof_end_frame(p); }
    CHECK(wb_prof_summary(p, "alive", &st) == 1 && st.last == 0.0f,
          "untouched counter records a 0 delta");

    /* ---- ring caps at last N ----------------------------------------- */
    /* alive now has 6 frames; push 10 more -> written 16, window n=8 */
    for (int f = 0; f < 10; f++) {
        wb_prof_begin_frame(p);
        *alive += 100.0;
        wb_prof_end_frame(p);
    }
    CHECK(wb_prof_summary(p, "alive", &st) == 1, "alive still reachable");
    CHECK(st.n == 8, "ring folded to last 8 frames");
    CHECK(st.last == 100.0f, "last frame delta is 100");
    CHECK(st.mmin >= 0.0f && st.mmax == 100.0f, "min/max folded over the window");

    /* ---- zone timing of real work ------------------------------------ */
    for (int f = 0; f < 2; f++) {
        wb_prof_begin_frame(p);
        WB_PROF_ZONE(p, "busy") { busy(0.002); }
        wb_prof_end_frame(p);
    }
    CHECK(wb_prof_summary(p, "busy", &st) == 1, "zone slot exists");
    CHECK(st.n == 2, "two frames of busy timing");
    CHECK(st.last > 0.0f, "zone measured real elapsed time");

    /* ---- nested (LIFO) zones ------------------------------------------ */
    wb_prof_begin_frame(p);
    WB_PROF_ZONE(p, "outer") {
        busy(0.001);
        WB_PROF_ZONE(p, "inner") { busy(0.001); }
    }
    wb_prof_end_frame(p);
    wb_prof_stat sos, sis;
    CHECK(wb_prof_summary(p, "outer", &sos) == 1 && wb_prof_summary(p, "inner", &sis) == 1,
          "both nested zones recorded");
    CHECK(sos.last > 0.0f && sis.last > 0.0f, "nested zones each measured time");

    /* ---- a zone that does not run this frame reads 0 ------------------ */
    for (int f = 0; f < 3; f++) { wb_prof_begin_frame(p); wb_prof_end_frame(p); }
    CHECK(wb_prof_summary(p, "busy", &st) == 1 && st.last == 0.0f,
          "idle zone records 0 for frames it does not run");

    /* ---- report writes every slot -------------------------------------- */
    FILE *tmp = tmpfile();
    CHECK(tmp != NULL, "tmpfile for report");
    int shown = wb_prof_report(p, tmp);
    CHECK(shown >= 4, "report lists the slots (alive, busy, outer, inner)");
    if (tmp) fclose(tmp);

    /* ---- budget gate: a frame that blows its allowance trips it ---------- */
    wb_prof *q = wb_prof_new(4);
    double *trips = wb_prof_counter(q, "trips");
    wb_prof_set_budget(q, "trips", 5.0);          /* delta must stay < 5 */
    for (int f = 0; f < 2; f++) {
        wb_prof_begin_frame(q);
        *trips += 10.0;
        wb_prof_end_frame(q);
    }
    CHECK(wb_prof_overran(q) == 1, "over-budget frame trips the gate");
    CHECK(wb_prof_total_overruns(q) == 2, "every over-budget frame counted");

    double *ok = wb_prof_counter(q, "ok");
    wb_prof_set_budget(q, "ok", 5.0);
    for (int f = 0; f < 2; f++) {
        wb_prof_begin_frame(q);
        *ok += 1.0;
        wb_prof_end_frame(q);
    }
    CHECK(wb_prof_overran(q) == 0, "within-budget frame does not trip");

    /* establish the zone first, then budget its steady-state time */
    wb_prof_begin_frame(q);
    WB_PROF_ZONE(q, "busyzone") { busy(0.001); }
    wb_prof_end_frame(q);
    wb_prof_set_budget(q, "busyzone", 0.000001f);  /* 1us: busy() blows it */
    wb_prof_begin_frame(q);
    WB_PROF_ZONE(q, "busyzone") { busy(0.002); }
    wb_prof_end_frame(q);
    CHECK(wb_prof_overran(q) == 1, "tight zone time budget trips");

    wb_prof_set_budget(q, "busyzone", 10.0f);      /* 10s: nowhere near */
    wb_prof_begin_frame(q);
    WB_PROF_ZONE(q, "busyzone") { busy(0.001); }
    wb_prof_end_frame(q);
    CHECK(wb_prof_overran(q) == 0, "generous budget does not trip");
    wb_prof_dispose(q);

    /* ---- null-safety ---------------------------------------------------- */
    wb_prof_summary(NULL, "x", &st);
    st.n = 0; (void)st;
    CHECK(wb_prof_counter(NULL, "x") == NULL, "null profiler counter is null");
    CHECK(wb_prof_report(NULL, NULL) == 0, "null profiler reports 0");
    wb_prof_dispose(p);

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

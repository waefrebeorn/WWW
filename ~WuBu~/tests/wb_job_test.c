/*
 * wb_job_test.c -- LP-06 stone 1 gate: the deterministic job graph.
 *
 * Proves dependency-ordered execution: independent jobs run, a diamond runs in
 * topological order (A before B,C before D), a shared counter aggregates, a
 * cycle is detected and nothing runs, and the run is deterministic.
 */
#include "wb_job.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

/* a tiny execution log */
static char g_log[64];
static int  g_log_n;
static void log_job(void *user) { g_log[g_log_n++] = (char)(int)(size_t)user; }

static void reset(void) { memset(g_log, 0, sizeof g_log); g_log_n = 0; }

static void count_job(void *user) { (*(int *)user)++; }

int main(void) {
    /* ---- independent jobs run in add order ---- */
    reset();
    wb_job_graph *g = wb_job_graph_new(8);
    wb_job_add(g, log_job, (void *)(size_t)'a', NULL, 0);
    wb_job_add(g, log_job, (void *)(size_t)'b', NULL, 0);
    wb_job_add(g, log_job, (void *)(size_t)'c', NULL, 0);
    int ran = wb_job_run(g);
    CHECK(ran == 3, "three independent jobs all run");
    CHECK(g_log_n == 3 && g_log[0] == 'a' && g_log[1] == 'b' && g_log[2] == 'c',
          "independent jobs run in add order");
    wb_job_graph_free(g);

    /* ---- diamond: A -> B,C -> D ---- */
    reset();
    g = wb_job_graph_new(8);
    wb_job *a = wb_job_add(g, log_job, (void *)(size_t)'A', NULL, 0);
    wb_job *b = wb_job_add(g, log_job, (void *)(size_t)'B', (wb_job *[]){a}, 1);
    wb_job *c = wb_job_add(g, log_job, (void *)(size_t)'C', (wb_job *[]){a}, 1);
    wb_job_add(g, log_job, (void *)(size_t)'D', (wb_job *[]){b, c}, 2);
    ran = wb_job_run(g);
    CHECK(ran == 4, "diamond runs all four jobs");
    CHECK(g_log_n == 4 && g_log[0] == 'A' && g_log[3] == 'D',
          "A runs first and D runs last");
    CHECK((g_log[1] == 'B' && g_log[2] == 'C') || (g_log[1] == 'C' && g_log[2] == 'B'),
          "B and C run between A and D");
    wb_job_graph_free(g);

    /* ---- shared counter aggregates across jobs ---- */
    g = wb_job_graph_new(16);
    int total = 0;
    for (int i = 0; i < 16; i++) wb_job_add(g, count_job, &total, NULL, 0);
    ran = wb_job_run(g);
    CHECK(ran == 16 && total == 16, "every job contributes to the shared counter");
    wb_job_graph_free(g);

    /* ---- a cycle is detected and nothing runs ---- */
    reset();
    g = wb_job_graph_new(4);
    a = wb_job_add(g, log_job, (void *)(size_t)'x', NULL, 0);
    b = wb_job_add(g, log_job, (void *)(size_t)'y', NULL, 0);
    a->deps = (wb_job *[]){b}; a->ndeps = 1; a->pending = 1;
    b->deps = (wb_job *[]){a}; b->ndeps = 1; b->pending = 1;
    ran = wb_job_run(g);
    CHECK(ran == -1, "a dependency cycle is detected");
    CHECK(g_log_n == 0, "nothing executes when the graph has a cycle");
    wb_job_graph_free(g);

    /* ---- a full graph rejects further jobs ---- */
    g = wb_job_graph_new(2);
    CHECK(wb_job_add(g, count_job, &total, NULL, 0) != NULL, "first job added");
    CHECK(wb_job_add(g, count_job, &total, NULL, 0) != NULL, "second job added");
    CHECK(wb_job_add(g, count_job, &total, NULL, 0) == NULL, "a full graph rejects a third job");
    wb_job_graph_free(g);

    /* ---- deterministic: the same graph runs the same way twice ---- */
    reset();
    g = wb_job_graph_new(8);
    int t1 = 0, t2 = 0;
    wb_job *p = wb_job_add(g, count_job, &t1, NULL, 0);
    wb_job_add(g, count_job, &t2, (wb_job *[]){p}, 1);
    wb_job_run(g);
    int first = t1 + t2;
    t1 = t2 = 0;
    /* rebuild and rerun to confirm identical aggregation */
    wb_job_graph_free(g);
    g = wb_job_graph_new(8);
    p = wb_job_add(g, count_job, &t1, NULL, 0);
    wb_job_add(g, count_job, &t2, (wb_job *[]){p}, 1);
    wb_job_run(g);
    CHECK(first == 2 && (t1 + t2) == 2, "repeated runs aggregate identically");
    wb_job_graph_free(g);

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

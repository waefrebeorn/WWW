/*
 * wb_job.c -- the job graph (LP-06 stone 1). See wb_job.h.
 *
 * Kahn's algorithm with a pending counter per job. Dependencies are matched by
 * pointer; a job may be a dependency of many others. Execution is on the
 * calling thread and deterministic: the ready queue is FIFO over the add order.
 */
#include "wb_job.h"

#include <stdlib.h>

wb_job_graph *wb_job_graph_new(int cap) {
    if (cap < 1) cap = 1;
    wb_job_graph *g = (wb_job_graph *)calloc(1, sizeof(*g));
    if (!g) return NULL;
    g->jobs = (wb_job **)calloc((size_t)cap, sizeof(wb_job *));
    if (!g->jobs) { free(g); return NULL; }
    g->cap = cap;
    return g;
}

void wb_job_graph_free(wb_job_graph *g) {
    if (!g) return;
    for (int i = 0; i < g->njobs; i++) free(g->jobs[i]);
    free(g->jobs);
    free(g);
}

wb_job *wb_job_add(wb_job_graph *g, wb_job_fn fn, void *user,
                   wb_job **deps, int ndeps) {
    if (!g || !fn || g->njobs >= g->cap) return NULL;
    wb_job *j = (wb_job *)calloc(1, sizeof(*j));
    if (!j) return NULL;
    j->fn = fn;
    j->user = user;
    j->deps = deps;
    j->ndeps = ndeps > 0 ? ndeps : 0;
    j->pending = j->ndeps;
    g->jobs[g->njobs++] = j;
    return j;
}

int wb_job_run(wb_job_graph *g) {
    if (!g) return -1;
    /* ready queue: indices of jobs whose pending count is 0 */
    int *ready = (int *)malloc((size_t)g->njobs * sizeof(int));
    if (!ready && g->njobs > 0) return -1;
    int head = 0, tail = 0;
    for (int i = 0; i < g->njobs; i++)
        if (g->jobs[i]->pending == 0) ready[tail++] = i;

    int ran = 0;
    while (head < tail) {
        wb_job *j = g->jobs[ready[head++]];
        if (j->fn) j->fn(j->user);
        ran++;
        /* decrement every job that lists this one as a dependency */
        for (int i = 0; i < g->njobs; i++) {
            wb_job *k = g->jobs[i];
            for (int d = 0; d < k->ndeps; d++) {
                if (k->deps[d] == j && k->pending > 0) {
                    if (--k->pending == 0) ready[tail++] = i;
                    break;
                }
            }
        }
    }
    free(ready);
    return ran == g->njobs ? ran : -1;   /* -1 => cycle (some jobs never ran) */
}

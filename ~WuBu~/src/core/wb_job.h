/*
 * wb_job.h -- the job graph (LP-06 stone 1).
 *
 * A job is a function plus its dependencies; the graph executes them in
 * dependency order. This first stone is the deterministic skeleton: it runs
 * on the calling thread, in topological order, with a pending-dependency
 * counter per job -- the same counter discipline a threaded scheduler uses,
 * minus the races. The worker-pool backend is the next stone.
 *
 * C11, no deps, no allocation outside wb_job_graph_new.
 */
#ifndef WB_JOB_H
#define WB_JOB_H

#include <stddef.h>

typedef void (*wb_job_fn)(void *user);

typedef struct wb_job {
    wb_job_fn     fn;
    void         *user;
    struct wb_job **deps;
    int           ndeps;
    int           pending;   /* unmet dependencies */
} wb_job;

typedef struct {
    wb_job **jobs;
    int      njobs;
    int      cap;
} wb_job_graph;

wb_job_graph *wb_job_graph_new(int cap);
void          wb_job_graph_free(wb_job_graph *g);
wb_job       *wb_job_add(wb_job_graph *g, wb_job_fn fn, void *user,
                         wb_job **deps, int ndeps);

/* Run every job once, in dependency order. Returns the number of jobs run, or
 * -1 if the graph has a cycle (nothing is executed in that case). */
int  wb_job_run(wb_job_graph *g);

#endif /* WB_JOB_H */

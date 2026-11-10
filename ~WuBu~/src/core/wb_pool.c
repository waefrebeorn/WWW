/*
 * wb_pool.c -- worker pool (LP-06 stone 2). See wb_pool.h.
 */
#include "wb_pool.h"

#include <pthread.h>
#include <stdlib.h>

typedef struct wb_job {
    wb_pool_fn       fn;
    void            *user;
    struct wb_job   *next;
} wb_job;

struct wb_pool {
    pthread_t     *threads;
    int            nthreads;
    pthread_mutex_t m;
    pthread_cond_t  cv;
    wb_job         *head, *tail;
    int            pending;    /* submitted but not finished */
    int            shutdown;
};

static void *wb_pool_worker(void *arg) {
    wb_pool *p = (wb_pool *)arg;
    for (;;) {
        pthread_mutex_lock(&p->m);
        while (!p->shutdown && !p->head)
            pthread_cond_wait(&p->cv, &p->m);
        if (p->shutdown && !p->head) { pthread_mutex_unlock(&p->m); return NULL; }
        wb_job *j = p->head;
        p->head = j->next;
        if (!p->head) p->tail = NULL;
        pthread_mutex_unlock(&p->m);

        if (j->fn) j->fn(j->user);
        free(j);

        pthread_mutex_lock(&p->m);
        if (--p->pending == 0) pthread_cond_broadcast(&p->cv);
        pthread_mutex_unlock(&p->m);
    }
}

wb_pool *wb_pool_new(int nthreads) {
    if (nthreads < 1) nthreads = 1;
    wb_pool *p = (wb_pool *)calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->threads = (pthread_t *)calloc((size_t)nthreads, sizeof(pthread_t));
    if (!p->threads) { free(p); return NULL; }
    pthread_mutex_init(&p->m, NULL);
    pthread_cond_init(&p->cv, NULL);
    p->nthreads = nthreads;
    for (int i = 0; i < nthreads; i++)
        if (pthread_create(&p->threads[i], NULL, wb_pool_worker, p) != 0) {
            /* roll back the threads we started */
            pthread_mutex_lock(&p->m);
            p->shutdown = 1;
            pthread_cond_broadcast(&p->cv);
            pthread_mutex_unlock(&p->m);
            for (int k = 0; k < i; k++) pthread_join(p->threads[k], NULL);
            free(p->threads); free(p);
            return NULL;
        }
    return p;
}

void wb_pool_free(wb_pool *p) {
    if (!p) return;
    wb_pool_wait(p);
    pthread_mutex_lock(&p->m);
    p->shutdown = 1;
    pthread_cond_broadcast(&p->cv);
    pthread_mutex_unlock(&p->m);
    for (int i = 0; i < p->nthreads; i++) pthread_join(p->threads[i], NULL);
    pthread_mutex_destroy(&p->m);
    pthread_cond_destroy(&p->cv);
    free(p->threads);
    free(p);
}

int wb_pool_submit(wb_pool *p, wb_pool_fn fn, void *user) {
    if (!p || !fn) return -1;
    wb_job *j = (wb_job *)calloc(1, sizeof(*j));
    if (!j) return -1;
    j->fn = fn; j->user = user;
    pthread_mutex_lock(&p->m);
    if (p->shutdown) { pthread_mutex_unlock(&p->m); free(j); return -1; }
    if (p->tail) p->tail->next = j; else p->head = j;
    p->tail = j;
    p->pending++;
    pthread_cond_signal(&p->cv);
    pthread_mutex_unlock(&p->m);
    return 0;
}

void wb_pool_wait(wb_pool *p) {
    if (!p) return;
    pthread_mutex_lock(&p->m);
    while (p->pending > 0) pthread_cond_wait(&p->cv, &p->m);
    pthread_mutex_unlock(&p->m);
}

int wb_pool_nthreads(const wb_pool *p) { return p ? p->nthreads : 0; }

/* ---- deterministic parallel-for ------------------------------------------ */

typedef struct {
    wb_pool_map_fn fn;
    int start, end, slot;
    void *user;
} wb_map_job;

static void wb_map_trampoline(void *arg) {
    wb_map_job *j = (wb_map_job *)arg;
    j->fn(j->start, j->end, j->slot, j->user);
    free(j);
}

void wb_pool_map(wb_pool *p, wb_pool_map_fn fn, int n, int nslots, void *user) {
    if (!p || !fn || n <= 0 || nslots <= 0) return;
    if (nslots > n) nslots = n;
    for (int i = 0; i < nslots; i++) {
        int start = (int)((long)i * n / nslots);
        int end   = (int)((long)(i + 1) * n / nslots);
        wb_map_job *j = (wb_map_job *)malloc(sizeof(*j));
        if (!j) return;               /* out of memory: stop submitting */
        j->fn = fn; j->start = start; j->end = end; j->slot = i; j->user = user;
        if (wb_pool_submit(p, wb_map_trampoline, j) != 0) { free(j); return; }
    }
    wb_pool_wait(p);
}

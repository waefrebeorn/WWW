/*
 * wb_access_test.c -- LP-07 stone 1 gate: data-access validation.
 *
 * Proves acquire/release balance, that a safe point is only clean when nothing
 * is in flight, that releasing an unheld resource is an error, and that jobs
 * running on the worker pool leave no resource held once the batch completes.
 */
#include "wb_access.h"
#include "wb_pool.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

#define RES_A 1001u
#define RES_B 1002u
#define RES_C 1003u

/* a job that acquires a resource, does a little work, and releases it */
typedef struct { wb_access *acc; uint32_t id; } job_ctx;
static void acquire_release_job(void *user) {
    job_ctx *c = (job_ctx *)user;
    wb_access_acquire(c->acc, c->id);
    /* "work" -- no touching the resource outside the tracked window */
    volatile int x = 0;
    for (int i = 0; i < 1000; i++) x += i;
    (void)x;
    wb_access_release(c->acc, c->id);
}

int main(void) {
    wb_access a;
    wb_access_init(&a, 8);

    CHECK(wb_access_clean(&a), "a fresh tracker is clean");
    CHECK(wb_access_held(&a) == 0, "nothing is in flight");

    CHECK(wb_access_acquire(&a, RES_A) == 0, "acquire A");
    CHECK(wb_access_refs(&a, RES_A) == 1, "A has one holder");
    CHECK(wb_access_held(&a) == 1, "one resource in flight");
    CHECK(!wb_access_clean(&a), "not clean while A is held");

    CHECK(wb_access_acquire(&a, RES_A) == 0, "acquire A again");
    CHECK(wb_access_refs(&a, RES_A) == 2, "A has two holders");
    CHECK(wb_access_held(&a) == 1, "still one distinct resource");

    CHECK(wb_access_acquire(&a, RES_B) == 0, "acquire B");
    CHECK(wb_access_held(&a) == 2, "two resources in flight");

    CHECK(wb_access_release(&a, RES_A) == 0, "release one A holder");
    CHECK(wb_access_refs(&a, RES_A) == 1 && wb_access_held(&a) == 2, "A still held once");

    CHECK(wb_access_release(&a, RES_A) == 0, "release the other A holder");
    CHECK(wb_access_refs(&a, RES_A) == 0 && wb_access_held(&a) == 1, "A is free, B remains");

    CHECK(wb_access_release(&a, RES_B) == 0, "release B");
    CHECK(wb_access_clean(&a), "safe point is clean after all releases");

    /* ---- error cases ---- */
    CHECK(wb_access_release(&a, RES_C) == -1, "releasing an unheld resource is an error");
    CHECK(wb_access_release(&a, 9999u) == -1, "releasing an unknown resource is an error");
    CHECK(wb_access_refs(&a, RES_C) == 0, "a failed release does not create a resource");

    /* ---- capacity ---- */
    wb_access small;
    wb_access_init(&small, 2);
    CHECK(wb_access_acquire(&small, 1u) == 0, "small: first");
    CHECK(wb_access_acquire(&small, 2u) == 0, "small: second");
    CHECK(wb_access_acquire(&small, 3u) == -1, "a full table rejects a new resource");
    CHECK(wb_access_release(&small, 1u) == 0 && wb_access_release(&small, 2u) == 0, "small: release both");
    wb_access_free(&small);

    /* ---- integration: jobs on the pool leave nothing held ---- */
    wb_pool *p = wb_pool_new(4);
    job_ctx ctx[16];
    for (int i = 0; i < 16; i++) {
        ctx[i].acc = &a;
        ctx[i].id = (uint32_t)(2000 + (i % 3));   /* three resources, contended */
        wb_pool_submit(p, acquire_release_job, &ctx[i]);
    }
    wb_pool_wait(p);
    CHECK(wb_access_clean(&a), "no resource is held after the batch completes");
    CHECK(wb_access_held(&a) == 0, "the pool batch left nothing in flight");
    wb_pool_free(p);

    wb_access_free(&a);
    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

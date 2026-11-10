/*
 * wb_access.h -- data-access validation (LP-07 stone 1).
 *
 * Tracks which resources have in-flight jobs. A job acquires a resource before
 * it touches it and releases it when done; a safe point (end of frame, end of
 * a job batch) asserts that nothing is still held. That is the invariant that
 * makes a threaded pool safe: a resource with a non-zero refcount is "in
 * flight" and must not be freed, remapped, or handed to another job.
 *
 * C11, no deps. Deterministic: a fixed table, linear scan.
 */
#ifndef WB_ACCESS_H
#define WB_ACCESS_H

#include <stdint.h>

typedef struct {
    uint32_t id;
    int      refs;     /* in-flight jobs holding this resource */
} wb_access_res;

typedef struct {
    wb_access_res *res;
    int            n, cap;
} wb_access;

void wb_access_init(wb_access *a, int cap);
void wb_access_free(wb_access *a);

/* +1 / -1 a resource. Returns 0 on success, -1 on error (release of an
 * unheld resource, or a full table). */
int  wb_access_acquire(wb_access *a, uint32_t id);
int  wb_access_release(wb_access *a, uint32_t id);

int  wb_access_refs(const wb_access *a, uint32_t id);   /* 0 if unknown */
int  wb_access_held(const wb_access *a);                /* #resources with refs>0 */
int  wb_access_clean(const wb_access *a);               /* 1 if nothing is held */

#endif /* WB_ACCESS_H */

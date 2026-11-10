/*
 * wb_access.c -- data-access validation (LP-07 stone 1). See wb_access.h.
 */
#include "wb_access.h"

#include <stdlib.h>

void wb_access_init(wb_access *a, int cap) {
    if (!a) return;
    if (cap < 1) cap = 1;
    a->res = (wb_access_res *)calloc((size_t)cap, sizeof(wb_access_res));
    a->n = 0;
    a->cap = a->res ? cap : 0;
}

void wb_access_free(wb_access *a) {
    if (!a) return;
    free(a->res);
    a->res = NULL; a->n = 0; a->cap = 0;
}

static wb_access_res *wb_access_find(wb_access *a, uint32_t id) {
    for (int i = 0; i < a->n; i++)
        if (a->res[i].id == id) return &a->res[i];
    return NULL;
}

int wb_access_acquire(wb_access *a, uint32_t id) {
    if (!a) return -1;
    wb_access_res *r = wb_access_find(a, id);
    if (r) { r->refs++; return 0; }
    if (a->n >= a->cap) return -1;             /* table full */
    a->res[a->n].id = id;
    a->res[a->n].refs = 1;
    a->n++;
    return 0;
}

int wb_access_release(wb_access *a, uint32_t id) {
    if (!a) return -1;
    wb_access_res *r = wb_access_find(a, id);
    if (!r || r->refs <= 0) return -1;         /* releasing an unheld resource */
    r->refs--;
    return 0;
}

int wb_access_refs(const wb_access *a, uint32_t id) {
    if (!a) return 0;
    for (int i = 0; i < a->n; i++)
        if (a->res[i].id == id) return a->res[i].refs;
    return 0;
}

int wb_access_held(const wb_access *a) {
    if (!a) return 0;
    int held = 0;
    for (int i = 0; i < a->n; i++) if (a->res[i].refs > 0) held++;
    return held;
}

int wb_access_clean(const wb_access *a) { return wb_access_held(a) == 0; }

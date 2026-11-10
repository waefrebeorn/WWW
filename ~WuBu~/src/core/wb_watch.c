/*
 * wb_watch.c -- file change detection (LP-38 stone 1). See wb_watch.h.
 */
#include "wb_watch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t wb_watch_hash_file(const char *path, uint64_t *size_out, int *exists) {
    FILE *f = fopen(path, "rb");
    if (!f) { if (exists) *exists = 0; if (size_out) *size_out = 0; return 0; }
    uint64_t h = 1469598103934665603ull, size = 0;
    unsigned char buf[4096];
    size_t got;
    while ((got = fread(buf, 1, sizeof buf, f)) > 0) {
        for (size_t i = 0; i < got; i++) { h ^= buf[i]; h *= 1099511628211ull; }
        size += got;
    }
    fclose(f);
    if (exists) *exists = 1;
    if (size_out) *size_out = size;
    return h;
}

static wb_watch_entry *wb_watch_find(wb_watch *w, const char *path) {
    if (!w || !w->entries) return NULL;
    for (uint32_t i = 0; i < w->n; i++)
        if (strcmp(w->entries[i].path, path) == 0) return &w->entries[i];
    return NULL;
}

int wb_watch_init(wb_watch *w, uint32_t cap) {
    if (!w || cap == 0) return -1;
    w->entries = (wb_watch_entry *)calloc(cap, sizeof(wb_watch_entry));
    if (!w->entries) return -1;
    w->n = 0;
    w->cap = cap;
    return 0;
}

void wb_watch_free(wb_watch *w) {
    if (!w) return;
    free(w->entries);
    w->entries = NULL;
    w->n = w->cap = 0;
}

void wb_watch_clear(wb_watch *w) {
    if (!w) return;
    w->n = 0;
}

int wb_watch_add(wb_watch *w, const char *path) {
    if (!w || !w->entries || !path) return -1;
    if (wb_watch_find(w, path)) return 0;          /* already watched */
    if (w->n == w->cap) return -1;
    wb_watch_entry *e = &w->entries[w->n];
    snprintf(e->path, sizeof e->path, "%s", path);
    e->hash = wb_watch_hash_file(path, &e->size, &e->exists);
    w->n++;
    return 0;
}

int wb_watch_poll(wb_watch *w, const char *path) {
    wb_watch_entry *e = wb_watch_find(w, path);
    if (!e) return -1;
    uint64_t hash, size;
    int exists;
    hash = wb_watch_hash_file(path, &size, &exists);
    int changed = (exists != e->exists) || (exists && (hash != e->hash || size != e->size));
    if (changed) { e->hash = hash; e->size = size; e->exists = exists; }  /* edge-triggered */
    return changed;
}

int wb_watch_refresh(wb_watch *w, const char *path) {
    wb_watch_entry *e = wb_watch_find(w, path);
    if (!e) return -1;
    e->hash = wb_watch_hash_file(path, &e->size, &e->exists);
    return 0;
}

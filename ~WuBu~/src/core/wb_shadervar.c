/*
 * wb_shadervar.c -- shader variant keys (LP-37 stone 1). See wb_shadervar.h.
 */
#include "wb_shadervar.h"

#include <stdlib.h>
#include <string.h>

/* FNV-1a over a canonical form: the defines sorted, each prefixed by its length
 * so that {"AB","C"} and {"A","BC"} cannot collide. */
static uint64_t wb_shadervar_hash(const char *s, uint64_t h) {
    for (; *s; s++) { h ^= (uint8_t)*s; h *= 1099511628211ull; }
    return h;
}

static int wb_shadervar_cmp(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

uint64_t wb_shadervar_key(const char *const *defines, uint32_t n) {
    if (!defines || n == 0) return 0;
    const char **sorted = (const char **)malloc(n * sizeof(const char *));
    if (!sorted) return 0;
    memcpy(sorted, defines, n * sizeof(const char *));
    qsort(sorted, n, sizeof(const char *), wb_shadervar_cmp);

    uint64_t h = 1469598103934665603ull;
    const char *prev = NULL;
    for (uint32_t i = 0; i < n; i++) {
        if (prev && strcmp(prev, sorted[i]) == 0) continue;   /* collapse duplicates */
        prev = sorted[i];
        uint64_t len = (uint64_t)strlen(sorted[i]);
        h = wb_shadervar_hash(sorted[i], h);
        h ^= len; h *= 1099511628211ull;                      /* the separator */
    }
    free(sorted);
    return h;
}

int wb_shadervar_cache_init(wb_shadervar_cache *c, uint32_t cap) {
    if (!c || cap == 0) return -1;
    c->keys = (uint64_t *)malloc(cap * sizeof(uint64_t));
    if (!c->keys) return -1;
    c->n = 0;
    c->cap = cap;
    return 0;
}

void wb_shadervar_cache_free(wb_shadervar_cache *c) {
    if (!c) return;
    free(c->keys);
    c->keys = NULL;
    c->n = c->cap = 0;
}

void wb_shadervar_cache_clear(wb_shadervar_cache *c) {
    if (c) c->n = 0;
}

int wb_shadervar_is_compiled(const wb_shadervar_cache *c, uint64_t key) {
    if (!c || !c->keys) return 0;
    for (uint32_t i = 0; i < c->n; i++) if (c->keys[i] == key) return 1;
    return 0;
}

int wb_shadervar_mark_compiled(wb_shadervar_cache *c, uint64_t key) {
    if (!c || !c->keys) return 0;
    if (wb_shadervar_is_compiled(c, key)) return 0;
    if (c->n == c->cap) return 0;                 /* full: leave it unmarked */
    c->keys[c->n++] = key;
    return 1;
}

/*
 * wb_shadervar.h -- shader variant keys (LP-37 stone 1).
 *
 * A shader is not one program: every combination of defines (skin on/off,
 * shadows on/off, instancing, ...) is a distinct binary. The variant key is the
 * identity of such a combination -- an order-independent, duplicate-free hash of
 * the define set -- and the variant cache tracks which keys have already been
 * compiled so a request for a known variant never recompiles.
 *
 * C11, no deps.
 */
#ifndef WB_SHADERVAR_H
#define WB_SHADERVAR_H

#include <stdint.h>

/* The variant key for a set of define names. Order-independent and
 * duplicate-free: {A,B} and {B,A} hash the same. Returns 0 on empty input. */
uint64_t wb_shadervar_key(const char *const *defines, uint32_t n);

typedef struct {
    uint64_t  *keys;
    uint32_t   n, cap;
} wb_shadervar_cache;

int  wb_shadervar_cache_init(wb_shadervar_cache *c, uint32_t cap);
void wb_shadervar_cache_free(wb_shadervar_cache *c);
void wb_shadervar_cache_clear(wb_shadervar_cache *c);

/* 1 if the key is already in the cache, 0 otherwise. */
int  wb_shadervar_is_compiled(const wb_shadervar_cache *c, uint64_t key);
/* Mark a key compiled. Returns 1 if it was newly added, 0 if already present. */
int  wb_shadervar_mark_compiled(wb_shadervar_cache *c, uint64_t key);

#endif /* WB_SHADERVAR_H */

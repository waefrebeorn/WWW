/*
 * wb_shadervar_test.c -- LP-37 stone 1 gate: shader variant keys.
 *
 * Devil's-advocate cases: the key is order-independent, duplicates collapse,
 * length-prefixing prevents a boundary ambiguity, distinct define sets hash
 * distinctly, and the compiled-variant cache deduplicates and refuses when full.
 */
#include "wb_shadervar.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

int main(void) {
    /* ---- order independence ---- */
    const char *a[] = { "SKIN", "SHADOW", "INSTANCING" };
    const char *b[] = { "INSTANCING", "SKIN", "SHADOW" };
    const char *c[] = { "SHADOW", "INSTANCING", "SKIN" };
    uint64_t ka = wb_shadervar_key(a, 3);
    CHECK(ka == wb_shadervar_key(b, 3) && ka == wb_shadervar_key(c, 3), "the key is order-independent");
    CHECK(ka != 0, "a non-empty define set hashes non-zero");

    /* ---- duplicates collapse ---- */
    const char *dup[] = { "SKIN", "SKIN", "SKIN" };
    const char *one[] = { "SKIN" };
    CHECK(wb_shadervar_key(dup, 3) == wb_shadervar_key(one, 1), "duplicate defines collapse");

    /* ---- the length separator prevents a boundary ambiguity ---- */
    const char *bc[] = { "AB", "C" };
    const char *abc[] = { "A", "BC" };
    CHECK(wb_shadervar_key(bc, 2) != wb_shadervar_key(abc, 2), "a boundary shift is a different variant");

    /* ---- distinct sets hash distinctly ---- */
    const char *d1[] = { "SKIN" };
    const char *d2[] = { "SKIN", "SHADOW" };
    const char *d3[] = { "SHADOW" };
    CHECK(wb_shadervar_key(d1, 1) != wb_shadervar_key(d2, 2), "adding a define changes the key");
    CHECK(wb_shadervar_key(d1, 1) != wb_shadervar_key(d3, 1), "different defines differ");
    CHECK(wb_shadervar_key(d2, 2) != wb_shadervar_key(d3, 1), "a superset differs from its member");

    /* ---- the empty set ---- */
    CHECK(wb_shadervar_key(NULL, 3) == 0, "NULL defines hash to zero");
    CHECK(wb_shadervar_key(a, 0) == 0, "an empty define set hashes to zero");

    /* ---- the compiled-variant cache ---- */
    wb_shadervar_cache cache;
    CHECK(wb_shadervar_cache_init(&cache, 4) == 0, "the cache is created");
    CHECK(wb_shadervar_mark_compiled(&cache, ka) == 1, "a new variant is marked");
    CHECK(wb_shadervar_is_compiled(&cache, ka) == 1, "the variant is found");
    CHECK(wb_shadervar_mark_compiled(&cache, ka) == 0, "marking it twice is idempotent");
    CHECK(wb_shadervar_mark_compiled(&cache, wb_shadervar_key(d3, 1)) == 1, "a second variant is marked");
    CHECK(wb_shadervar_is_compiled(&cache, wb_shadervar_key(d3, 1)) == 1, "the second variant is found");
    CHECK(wb_shadervar_is_compiled(&cache, wb_shadervar_key(d2, 2)) == 0, "an unmarked variant is not found");

    /* ---- a full cache refuses to grow ---- */
    CHECK(wb_shadervar_mark_compiled(&cache, 111) == 1, "a third variant fits");
    CHECK(wb_shadervar_mark_compiled(&cache, 222) == 1, "a fourth variant fits");
    CHECK(wb_shadervar_mark_compiled(&cache, 333) == 0, "a full cache refuses a new variant");
    CHECK(wb_shadervar_is_compiled(&cache, 333) == 0, "the refused variant is absent");
    CHECK(wb_shadervar_mark_compiled(&cache, 111) == 0, "a re-mark of an existing key is still idempotent");

    /* ---- clear ---- */
    wb_shadervar_cache_clear(&cache);
    CHECK(wb_shadervar_is_compiled(&cache, ka) == 0, "clear empties the cache");
    CHECK(wb_shadervar_mark_compiled(&cache, ka) == 1, "a cleared cache accepts the key again");

    /* ---- determinism ---- */
    CHECK(wb_shadervar_key(a, 3) == ka, "the key is deterministic");

    /* ---- degenerate input ---- */
    CHECK(wb_shadervar_cache_init(NULL, 4) == -1, "a NULL cache is rejected");
    CHECK(wb_shadervar_cache_init(&cache, 0) == -1, "a zero capacity is rejected");
    CHECK(wb_shadervar_is_compiled(NULL, ka) == 0, "a NULL cache finds nothing");
    CHECK(wb_shadervar_mark_compiled(NULL, ka) == 0, "a NULL cache accepts nothing");

    wb_shadervar_cache_free(&cache);
    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

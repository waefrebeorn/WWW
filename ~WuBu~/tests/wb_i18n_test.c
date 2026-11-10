/*
 * wb_i18n_test.c -- LP-39 gate: localization string tables.
 *
 * Devil's-advocate cases: an overwrite replaces rather than appends, a missing key
 * falls through the whole chain to NULL (never a wrong-language string), a key
 * present only in a fallback is found, and a full table refuses to grow.
 */
#include "wb_i18n.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

int main(void) {
    wb_i18n_table en, fr;
    CHECK(wb_i18n_table_init(&en, "en", 8) == 0, "the English table is created");
    CHECK(wb_i18n_table_init(&fr, "fr", 8) == 0, "the French table is created");

    /* ---- add and get ---- */
    CHECK(wb_i18n_table_add(&en, "hello", "Hello") == 0, "a key is added");
    CHECK(strcmp(wb_i18n_table_get(&en, "hello"), "Hello") == 0, "the key is found");
    CHECK(wb_i18n_table_get(&en, "goodbye") == NULL, "a missing key is NULL");

    /* ---- overwrite replaces, not appends ---- */
    CHECK(wb_i18n_table_add(&en, "hello", "Hi") == 0, "the key is overwritten");
    CHECK(strcmp(wb_i18n_table_get(&en, "hello"), "Hi") == 0, "the new text is stored");
    CHECK(wb_i18n_table_add(&en, "bye", "Goodbye") == 0, "a second key is added");
    CHECK(strcmp(wb_i18n_table_get(&en, "hello"), "Hi") == 0, "the overwrite did not disturb the other key");

    /* ---- the fallback chain ---- */
    wb_i18n_table_add(&fr, "hello", "Bonjour");
    wb_i18n_table_add(&fr, "cat", "Chat");
    wb_i18n_locale loc;
    loc.primary = &fr;
    wb_i18n_table *fbs[1] = { &en };
    loc.fallbacks = fbs;
    loc.nfallbacks = 1;

    CHECK(strcmp(wb_i18n_localize(&loc, "hello"), "Bonjour") == 0, "the primary wins over the fallback");
    CHECK(strcmp(wb_i18n_localize(&loc, "bye"), "Goodbye") == 0, "a key only in the fallback is found");
    CHECK(strcmp(wb_i18n_localize(&loc, "cat"), "Chat") == 0, "a key only in the primary is found");
    CHECK(wb_i18n_localize(&loc, "missing") == NULL, "a key missing everywhere is NULL");

    /* ---- the fallback order is respected ---- */
    wb_i18n_table de;
    wb_i18n_table_init(&de, "de", 8);
    wb_i18n_table_add(&de, "bye", "Tschuss");
    wb_i18n_table *fbs2[2] = { &en, &de };
    loc.fallbacks = fbs2;
    loc.nfallbacks = 2;
    CHECK(strcmp(wb_i18n_localize(&loc, "bye"), "Goodbye") == 0, "the first fallback wins");
    loc.fallbacks = NULL;
    loc.nfallbacks = 0;
    CHECK(wb_i18n_localize(&loc, "bye") == NULL, "no fallbacks means a primary miss is NULL");

    /* ---- a full table refuses to grow ---- */
    wb_i18n_table small;
    wb_i18n_table_init(&small, "es", 2);
    CHECK(wb_i18n_table_add(&small, "a", "A") == 0, "the first slot is taken");
    CHECK(wb_i18n_table_add(&small, "b", "B") == 0, "the second slot is taken");
    CHECK(wb_i18n_table_add(&small, "c", "C") == -1, "a full table refuses to grow");
    CHECK(wb_i18n_table_get(&small, "c") == NULL, "the refused key is absent");

    /* ---- strings that do not fit ---- */
    char longkey[200];
    memset(longkey, 'k', sizeof longkey - 1);
    longkey[sizeof longkey - 1] = '\0';
    CHECK(wb_i18n_table_add(&en, longkey, "x") == -1, "an over-long key is refused");
    char longtext[700];
    memset(longtext, 't', sizeof longtext - 1);
    longtext[sizeof longtext - 1] = '\0';
    CHECK(wb_i18n_table_add(&en, "long", longtext) == -1, "an over-long text is refused");
    CHECK(wb_i18n_table_add(&en, NULL, "x") == -1, "a NULL key is refused");
    CHECK(wb_i18n_table_add(&en, "x", NULL) == -1, "a NULL text is refused");

    /* ---- determinism ---- */
    wb_i18n_table a, b;
    wb_i18n_table_init(&a, "en", 8);
    wb_i18n_table_init(&b, "en", 8);
    wb_i18n_table_add(&a, "k1", "v1");
    wb_i18n_table_add(&b, "k1", "v1");
    wb_i18n_table_add(&a, "k2", "v2");
    wb_i18n_table_add(&b, "k2", "v2");
    CHECK(strcmp(wb_i18n_table_get(&a, "k1"), wb_i18n_table_get(&b, "k1")) == 0, "lookup is deterministic");
    CHECK(strcmp(wb_i18n_table_get(&a, "k2"), wb_i18n_table_get(&b, "k2")) == 0, "a second lookup is deterministic");

    /* ---- degenerate input ---- */
    CHECK(wb_i18n_table_init(NULL, "en", 4) == -1, "a NULL table is rejected");
    CHECK(wb_i18n_table_init(&en, "en", 0) == -1, "a zero capacity is rejected");
    CHECK(wb_i18n_table_get(NULL, "hello") == NULL, "a NULL table returns NULL");
    CHECK(wb_i18n_table_get(&en, NULL) == NULL, "a NULL key returns NULL");
    CHECK(wb_i18n_localize(NULL, "hello") == NULL, "a NULL locale returns NULL");
    CHECK(wb_i18n_localize(&loc, NULL) == NULL, "a NULL key on a locale returns NULL");

    wb_i18n_table_free(&en);
    wb_i18n_table_free(&fr);
    wb_i18n_table_free(&de);
    wb_i18n_table_free(&small);
    wb_i18n_table_free(&a);
    wb_i18n_table_free(&b);
    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

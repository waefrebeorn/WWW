/*
 * wb_i18n.c -- localization string tables (LP-39). See wb_i18n.h.
 */
#include "wb_i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int wb_i18n_table_init(wb_i18n_table *t, const char *lang, uint32_t cap) {
    if (!t || cap == 0) return -1;
    t->entries = (wb_i18n_entry *)calloc(cap, sizeof(wb_i18n_entry));
    if (!t->entries) return -1;
    t->n = 0;
    t->cap = cap;
    t->lang = lang;
    return 0;
}

void wb_i18n_table_free(wb_i18n_table *t) {
    if (!t) return;
    free(t->entries);
    t->entries = NULL;
    t->n = t->cap = 0;
}

const char *wb_i18n_table_get(const wb_i18n_table *t, const char *key) {
    if (!t || !t->entries || !key) return NULL;
    for (uint32_t i = 0; i < t->n; i++)
        if (strcmp(t->entries[i].key, key) == 0) return t->entries[i].text;
    return NULL;
}

int wb_i18n_table_add(wb_i18n_table *t, const char *key, const char *text) {
    if (!t || !t->entries || !key || !text) return -1;
    if (strlen(key) >= sizeof t->entries[0].key) return -1;
    if (strlen(text) >= sizeof t->entries[0].text) return -1;
    for (uint32_t i = 0; i < t->n; i++) {
        if (strcmp(t->entries[i].key, key) == 0) {   /* overwrite in place */
            snprintf(t->entries[i].text, sizeof t->entries[i].text, "%s", text);
            return 0;
        }
    }
    if (t->n == t->cap) return -1;
    wb_i18n_entry *e = &t->entries[t->n++];
    snprintf(e->key, sizeof e->key, "%s", key);
    snprintf(e->text, sizeof e->text, "%s", text);
    return 0;
}

const char *wb_i18n_localize(const wb_i18n_locale *loc, const char *key) {
    if (!loc || !key) return NULL;
    if (loc->primary) {
        const char *s = wb_i18n_table_get(loc->primary, key);
        if (s) return s;
    }
    if (loc->fallbacks) {
        for (uint32_t i = 0; i < loc->nfallbacks; i++) {
            const char *s = wb_i18n_table_get(loc->fallbacks[i], key);
            if (s) return s;
        }
    }
    return NULL;
}

/*
 * wb_i18n.h -- localization string tables (LP-39).
 *
 * A locale is a primary language table plus a fallback chain. Lookup walks the
 * primary first, then each fallback in order; a key missing everywhere yields
 * NULL, never a crash and never a wrong-language string. Adding a key that
 * already exists overwrites it, so a later pack can patch a translation.
 *
 * C11, no deps.
 */
#ifndef WB_I18N_H
#define WB_I18N_H

#include <stdint.h>

typedef struct {
    char     key[128];
    char     text[512];
} wb_i18n_entry;

typedef struct {
    wb_i18n_entry *entries;
    uint32_t       n, cap;
    const char    *lang;
} wb_i18n_table;

typedef struct {
    wb_i18n_table  *primary;
    wb_i18n_table **fallbacks;
    uint32_t        nfallbacks;
} wb_i18n_locale;

int  wb_i18n_table_init(wb_i18n_table *t, const char *lang, uint32_t cap);
void wb_i18n_table_free(wb_i18n_table *t);

/* Insert or overwrite a key. Returns 0 on success, -1 if the table is full or a
 * string does not fit. */
int  wb_i18n_table_add(wb_i18n_table *t, const char *key, const char *text);
const char *wb_i18n_table_get(const wb_i18n_table *t, const char *key);

/* Walk the primary table, then the fallbacks in order. NULL if not found. */
const char *wb_i18n_localize(const wb_i18n_locale *loc, const char *key);

#endif /* WB_I18N_H */

/*
 * wb_prov.c -- asset provenance and licensing (LP-79 stone 1). See wb_prov.h.
 */
#include "wb_prov.h"

#include <stdio.h>
#include <string.h>

static int wb_prov_valid_source(uint32_t source) {
    return (unsigned)source <= (unsigned)WB_PROV_GENERATED;
}

void wb_prov_init(wb_prov_registry *p) {
    if (!p) return;
    memset(p, 0, sizeof *p);
}

uint32_t wb_prov_count(const wb_prov_registry *p) {
    return p ? p->n : 0;
}

uint32_t wb_prov_allow_count(const wb_prov_registry *p) {
    return p ? p->nallow : 0;
}

int wb_prov_allow(wb_prov_registry *p, const char *spdx) {
    if (!p || !spdx || !*spdx) return -1;
    if (strlen(spdx) >= WB_PROV_LICENSE) return -1;
    if (p->nallow >= WB_PROV_MAX_ALLOW) return -1;
    for (uint32_t i = 0; i < p->nallow; i++)
        if (strcmp(p->allow[i], spdx) == 0) return 0;   /* already allowed */
    strcpy(p->allow[p->nallow++], spdx);
    return 0;
}

int wb_prov_is_allowed(const wb_prov_registry *p, const char *spdx) {
    if (!p || !spdx) return 0;
    for (uint32_t i = 0; i < p->nallow; i++)
        if (strcmp(p->allow[i], spdx) == 0) return 1;
    return 0;
}

int wb_prov_add(wb_prov_registry *p, const wb_prov_rec *rec) {
    if (!p || !rec) return -1;
    if (p->n >= WB_PROV_MAX) return -1;
    if (!rec->path[0]) return -1;
    if (strlen(rec->path) >= WB_PROV_NAME) return -1;
    if (!wb_prov_valid_source(rec->source)) return -1;
    if (strlen(rec->license) >= WB_PROV_LICENSE) return -1;
    if (strlen(rec->model) >= WB_PROV_MODEL) return -1;
    if (strlen(rec->prompt) >= WB_PROV_HASH) return -1;
    if (strlen(rec->seed) >= WB_PROV_SEED) return -1;
    if (wb_prov_find(p, rec->path)) return -1;
    /* A generated asset that cannot be traced to a model and a prompt cannot be
     * audited later, so it is not a record this registry will carry. */
    if (rec->source == WB_PROV_GENERATED && (!rec->model[0] || !rec->prompt[0]))
        return -1;

    p->recs[p->n++] = *rec;
    return 0;
}

const wb_prov_rec *wb_prov_find(const wb_prov_registry *p, const char *path) {
    if (!p || !path || !*path) return NULL;
    for (uint32_t i = 0; i < p->n; i++)
        if (strcmp(p->recs[i].path, path) == 0) return &p->recs[i];
    return NULL;
}

int wb_prov_check_rec(const wb_prov_registry *p, const wb_prov_rec *rec, uint32_t *reason) {
    if (reason) *reason = WB_PROV_OK;
    if (!p || !rec) {
        if (reason) *reason = WB_PROV_ERR_UNKNOWN_ASSET;
        return -1;
    }
    if (!rec->license[0]) {
        if (reason) *reason = WB_PROV_ERR_NO_LICENSE;
        return -1;
    }
    if (!wb_prov_is_allowed(p, rec->license)) {
        if (reason) *reason = WB_PROV_ERR_NOT_ALLOWED;
        return -1;
    }
    if (rec->source == WB_PROV_GENERATED) {
        if (!rec->model[0]) {
            if (reason) *reason = WB_PROV_ERR_GENERATED_NO_MODEL;
            return -1;
        }
        if (!rec->prompt[0]) {
            if (reason) *reason = WB_PROV_ERR_GENERATED_NO_PROMPT;
            return -1;
        }
    }
    return 0;
}

int wb_prov_check(const wb_prov_registry *p, const char *path, uint32_t *reason) {
    const wb_prov_rec *r = wb_prov_find(p, path);
    if (!r) {
        if (reason) *reason = WB_PROV_ERR_UNKNOWN_ASSET;
        return -1;
    }
    return wb_prov_check_rec(p, r, reason);
}

const wb_prov_rec *wb_prov_first_refused(const wb_prov_registry *p) {
    if (!p) return NULL;
    for (uint32_t i = 0; i < p->n; i++)
        if (wb_prov_check_rec(p, &p->recs[i], NULL) != 0) return &p->recs[i];
    return NULL;
}

const char *wb_prov_source_name(uint32_t source) {
    switch (source) {
    case WB_PROV_AUTHORED:  return "authored";
    case WB_PROV_IMPORTED:  return "imported";
    case WB_PROV_GENERATED: return "generated";
    default:                return "?";
    }
}

const char *wb_prov_reason_name(uint32_t reason) {
    switch (reason) {
    case WB_PROV_OK:                    return "ok";
    case WB_PROV_ERR_UNKNOWN_ASSET:     return "unknown asset";
    case WB_PROV_ERR_NO_LICENSE:        return "no license";
    case WB_PROV_ERR_NOT_ALLOWED:       return "license not allowed";
    case WB_PROV_ERR_GENERATED_NO_MODEL: return "generated asset has no model";
    case WB_PROV_ERR_GENERATED_NO_PROMPT: return "generated asset has no prompt hash";
    default:                            return "?";
    }
}

size_t wb_prov_credits(const wb_prov_registry *p, char *out, size_t cap) {
    if (!p || !out || cap == 0) return 0;

    /* Measure first: a truncated credits file is worse than none, so the whole
     * block is written or nothing is. */
    size_t need = 1;                     /* the terminating NUL */
    for (uint32_t i = 0; i < p->n; i++) {
        const wb_prov_rec *r = &p->recs[i];
        need += (size_t)snprintf(NULL, 0, "%s | %s | %s", r->path, r->license,
                                 wb_prov_source_name(r->source));
        if (r->source == WB_PROV_GENERATED) {
            need += (size_t)snprintf(NULL, 0, " | model=%s | prompt=%s",
                                     r->model, r->prompt);
            if (r->seed[0])
                need += (size_t)snprintf(NULL, 0, " | seed=%s", r->seed);
        }
        need += 1;                       /* the newline */
    }
    if (need > cap) return 0;

    size_t at = 0;
    for (uint32_t i = 0; i < p->n; i++) {
        const wb_prov_rec *r = &p->recs[i];
        at += (size_t)sprintf(out + at, "%s | %s | %s", r->path, r->license,
                              wb_prov_source_name(r->source));
        if (r->source == WB_PROV_GENERATED) {
            at += (size_t)sprintf(out + at, " | model=%s | prompt=%s",
                                  r->model, r->prompt);
            if (r->seed[0])
                at += (size_t)sprintf(out + at, " | seed=%s", r->seed);
        }
        out[at++] = '\n';
    }
    out[at] = '\0';
    return at;
}

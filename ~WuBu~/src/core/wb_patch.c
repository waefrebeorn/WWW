/*
 * wb_patch.c -- line patch applier (LP-48 stone 1). See wb_patch.h.
 */
#include "wb_patch.h"

#include <stdlib.h>
#include <string.h>

void wb_patch_result_free(wb_patch_result *r) {
    if (!r) return;
    for (uint32_t i = 0; i < r->n; i++) free(r->lines[i]);
    free(r->lines);
    r->lines = NULL;
    r->n = 0;
}

int wb_patch_split(const char *text, wb_patch_result *out) {
    if (!text || !out) return -1;
    out->lines = NULL;
    out->n = 0;
    uint32_t cap = 0;
    const char *p = text;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (out->n == cap) {
            uint32_t ncap = cap ? cap * 2 : 8;
            char **nlines = (char **)realloc(out->lines, (size_t)ncap * sizeof(char *));
            if (!nlines) { wb_patch_result_free(out); return -1; }
            out->lines = nlines;
            cap = ncap;
        }
        char *line = (char *)malloc(len + 1);
        if (!line) { wb_patch_result_free(out); return -1; }
        memcpy(line, p, len);
        line[len] = '\0';
        out->lines[out->n++] = line;
        if (!nl) break;
        p = nl + 1;
        if (*p == '\0') break;   /* a trailing newline does not add an empty line */
    }
    return 0;
}

static int wb_patch_cmp(const void *a, const void *b) {
    const wb_patch_hunk *ha = *(const wb_patch_hunk *const *)a;
    const wb_patch_hunk *hb = *(const wb_patch_hunk *const *)b;
    // descending by line: apply the bottom of the file first
    if (ha->line != hb->line) return ha->line > hb->line ? -1 : 1;
    return 0;
}

int wb_patch_apply(const char *text, const wb_patch_hunk *hunks, uint32_t nhunks, wb_patch_result *out) {
    if (!text || !out) return -1;
    out->lines = NULL;
    out->n = 0;
    wb_patch_result src;
    if (wb_patch_split(text, &src) != 0) return -1;

    /* order the hunks bottom-up without touching the caller's array */
    const wb_patch_hunk **order = NULL;
    if (nhunks) {
        order = (const wb_patch_hunk **)malloc((size_t)nhunks * sizeof(*order));
        if (!order) { wb_patch_result_free(&src); return -1; }
        for (uint32_t i = 0; i < nhunks; i++) order[i] = &hunks[i];
        qsort(order, nhunks, sizeof(*order), wb_patch_cmp);
    }

    int rc = 0;
    for (uint32_t k = 0; k < nhunks && rc == 0; k++) {
        const wb_patch_hunk *h = order[k];
        if (h->line > src.n) { rc = -1; break; }
        if (h->nremove > src.n - h->line) { rc = -1; break; }
        uint32_t newn = src.n - h->nremove + h->nadd;
        /* Build into a fresh array: a shrinking realloc would poison the tail
         * slots this hunk still has to read. */
        char **nlines = (char **)malloc((size_t)newn * sizeof(char *));
        if (!nlines) { rc = -1; break; }
        uint32_t w = 0;
        for (uint32_t i = 0; i < h->line; i++) nlines[w++] = src.lines[i];   /* head, borrowed */
        uint32_t head = w;
        for (uint32_t i = 0; i < h->nadd && rc == 0; i++) {
            const char *s = h->add ? h->add[i] : "";
            char *copy = (char *)malloc(strlen(s) + 1);
            if (!copy) { rc = -1; break; }
            strcpy(copy, s);
            nlines[w++] = copy;
        }
        if (rc == 0) {
            for (uint32_t i = h->line + h->nremove; i < src.n; i++) nlines[w++] = src.lines[i];
            for (uint32_t i = 0; i < h->nremove; i++) free(src.lines[h->line + i]);
            free(src.lines);
            src.lines = nlines;
            src.n = newn;
        } else {
            for (uint32_t i = head; i < w; i++) free(nlines[i]);   /* only the copies */
            free(nlines);
        }
    }
    free(order);
    if (rc != 0) { wb_patch_result_free(&src); return -1; }
    *out = src;
    return 0;
}

char *wb_patch_join(const wb_patch_result *r) {
    if (!r) return NULL;
    size_t total = 1;
    for (uint32_t i = 0; i < r->n; i++) total += strlen(r->lines[i]) + 1;
    char *s = (char *)malloc(total);
    if (!s) return NULL;
    char *p = s;
    for (uint32_t i = 0; i < r->n; i++) {
        size_t len = strlen(r->lines[i]);
        memcpy(p, r->lines[i], len);
        p += len;
        *p++ = '\n';
    }
    *p = '\0';
    return s;
}

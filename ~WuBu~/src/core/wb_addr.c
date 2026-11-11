/*
 * wb_addr.c -- hierarchical place addresses (LP-104 stone 1). See wb_addr.h.
 */
#include "wb_addr.h"

#include <stdlib.h>
#include <string.h>

static int wb_addr_char_ok(char c) {
    if (c >= 'a' && c <= 'z') return 1;
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= '0' && c <= '9') return 1;
    return c == '_' || c == '-';
}

static char wb_addr_lower(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

int wb_addr_parse(const char *s, wb_addr *a) {
    if (!s || !a) return -1;
    /* a leading separator would create an empty first segment */
    if (s[0] == '/' || s[0] == '\0') return -1;

    memset(a, 0, sizeof *a);
    uint32_t seg = 0, w = 0;
    for (const char *p = s; ; p++) {
        char c = *p;
        if (c == '/' || c == '\0') {
            if (w == 0) return -1;                       /* an empty segment */
            if (seg >= WB_ADDR_MAX_SEG) return -1;       /* too many levels */
            a->seg[seg][w] = '\0';
            seg++;
            w = 0;
            if (c == '\0') break;
            if (p[1] == '\0') return -1;                 /* a trailing separator */
            if (p[1] == '/') return -1;                  /* an empty segment */
            continue;
        }
        if (!wb_addr_char_ok(c)) return -1;
        if (w + 1 >= WB_ADDR_SEG_MAX) return -1;         /* a segment too long */
        a->seg[seg][w++] = wb_addr_lower(c);
    }
    a->n = seg;
    return 0;
}

int wb_addr_format(const wb_addr *a, char *out, size_t cap) {
    if (!a || !out || cap == 0) return -1;
    size_t w = 0;
    for (uint32_t i = 0; i < a->n; i++) {
        size_t l = strlen(a->seg[i]);
        if (w + l + (i ? 1 : 0) + 1 > cap) return -1;
        if (i) out[w++] = '/';
        memcpy(out + w, a->seg[i], l);
        w += l;
    }
    out[w] = '\0';
    return 0;
}

int wb_addr_segment(const wb_addr *a, uint32_t i, char *out, size_t cap) {
    if (!a || !out || cap == 0 || i >= a->n) return -1;
    size_t l = strlen(a->seg[i]);
    if (l + 1 > cap) return -1;
    memcpy(out, a->seg[i], l + 1);
    return 0;
}

uint32_t wb_addr_depth(const wb_addr *a) {
    return a ? a->n : 0;
}

int wb_addr_equal(const wb_addr *a, const wb_addr *b) {
    if (!a || !b || a->n != b->n) return 0;
    for (uint32_t i = 0; i < a->n; i++)
        if (strcmp(a->seg[i], b->seg[i]) != 0) return 0;
    return 1;
}

int wb_addr_contains(const wb_addr *a, const wb_addr *b) {
    if (!a || !b || a->n > b->n) return 0;
    for (uint32_t i = 0; i < a->n; i++)
        if (strcmp(a->seg[i], b->seg[i]) != 0) return 0;
    return 1;
}

int wb_addr_is_ancestor(const wb_addr *a, const wb_addr *b) {
    return a && b && a->n < b->n && wb_addr_contains(a, b);
}

int wb_addr_common(const wb_addr *a, const wb_addr *b, wb_addr *out) {
    if (!a || !b || !out) return -1;
    memset(out, 0, sizeof *out);
    uint32_t n = a->n < b->n ? a->n : b->n;
    uint32_t i = 0;
    while (i < n && strcmp(a->seg[i], b->seg[i]) == 0) i++;
    memcpy(out->seg, a->seg, i * sizeof out->seg[0]);
    out->n = i;
    return 0;
}

int wb_addr_valid(const char *s) {
    wb_addr a;
    return wb_addr_parse(s, &a) == 0;
}

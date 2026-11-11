/*
 * wb_qkey.c -- quadkey spatial queries (LP-108 stone 1). See wb_qkey.h.
 */
#include "wb_qkey.h"

#include <string.h>

int wb_qkey_valid(const char *qk) {
    if (!qk) return 0;
    size_t n = strlen(qk);
    if (n >= WB_QKEY_MAX) return 0;
    for (size_t i = 0; i < n; i++)
        if (qk[i] < '0' || qk[i] > '3') return 0;
    return 1;
}

uint32_t wb_qkey_level(const char *qk) {
    if (!wb_qkey_valid(qk)) return 0;
    return (uint32_t)strlen(qk);
}

int wb_qkey_encode(uint32_t level, uint32_t col, uint32_t row, char *out, size_t cap) {
    if (!out || cap == 0) return -1;
    if (level >= WB_QKEY_MAX || level + 1 > cap) return -1;
    if (col >> level || row >> level) return -1;      /* out of range for the level */

    for (uint32_t i = 0; i < level; i++) {
        uint32_t shift = level - 1 - i;
        uint32_t digit = ((row >> shift) & 1u) * 2u + ((col >> shift) & 1u);
        out[i] = (char)('0' + digit);
    }
    out[level] = '\0';
    return 0;
}

int wb_qkey_decode(const char *qk, uint32_t *level, uint32_t *col, uint32_t *row) {
    if (!wb_qkey_valid(qk)) return -1;
    uint32_t c = 0, r = 0;
    for (size_t i = 0; qk[i]; i++) {
        uint32_t d = (uint32_t)(qk[i] - '0');
        c = (c << 1) | (d & 1u);
        r = (r << 1) | (d >> 1);
    }
    if (level) *level = (uint32_t)strlen(qk);
    if (col) *col = c;
    if (row) *row = r;
    return 0;
}

int wb_qkey_equal(const char *a, const char *b) {
    if (!wb_qkey_valid(a) || !wb_qkey_valid(b)) return 0;
    return strcmp(a, b) == 0;
}

int wb_qkey_contains(const char *a, const char *b) {
    if (!wb_qkey_valid(a) || !wb_qkey_valid(b)) return 0;
    size_t la = strlen(a);
    return strncmp(a, b, la) == 0;      /* a is a prefix of b */
}

int wb_qkey_overlaps(const char *a, const char *b) {
    return wb_qkey_contains(a, b) || wb_qkey_contains(b, a);
}

int wb_qkey_adjacent(const char *a, const char *b) {
    uint32_t la, lb, ca, cb, ra, rb;
    if (wb_qkey_decode(a, &la, &ca, &ra) != 0) return 0;
    if (wb_qkey_decode(b, &lb, &cb, &rb) != 0) return 0;
    if (la != lb) return 0;             /* adjacency is only defined within a level */
    if (la == 0) return 0;              /* the root has no neighbours */
    uint32_t dc = ca > cb ? ca - cb : cb - ca;
    uint32_t dr = ra > rb ? ra - rb : rb - ra;
    /* exactly one axis differs by exactly one: an edge, not a corner */
    return (dc == 1 && dr == 0) || (dc == 0 && dr == 1);
}

int wb_qkey_enclose(const char *a, const char *b, char *out, size_t cap) {
    if (!wb_qkey_valid(a) || !wb_qkey_valid(b)) return -1;
    if (!out || cap == 0) return -1;
    size_t i = 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    if (i + 1 > cap) return -1;
    memcpy(out, a, i);
    out[i] = '\0';
    return 0;
}

int wb_qkey_parent(const char *qk, char *out, size_t cap) {
    if (!wb_qkey_valid(qk)) return -1;
    if (!out || cap == 0) return -1;
    size_t n = strlen(qk);
    if (n == 0) return -1;              /* the root has no parent */
    if (n > cap) return -1;
    memcpy(out, qk, n - 1);
    out[n - 1] = '\0';
    return 0;
}

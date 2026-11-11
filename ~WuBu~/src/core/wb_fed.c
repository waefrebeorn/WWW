/*
 * wb_fed.c -- federation capability grants (LP-56 stone 1). See wb_fed.h.
 */
#include "wb_fed.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t wb_fed_hash(const char *s) {
    uint64_t h = 1469598103934665603ULL;
    for (; s && *s; s++) {
        h ^= (unsigned char)*s;
        h *= 1099511628211ULL;
    }
    return h;
}

/* Does the field have a terminator inside its own storage? A full buffer with no
 * NUL is a field that could never round-trip, and strlen() on it would read past
 * the array -- so this checks the bound explicitly. */
static int wb_fed_fits(const char *s, size_t cap) {
    if (!s) return 0;
    for (size_t i = 0; i < cap; i++) if (s[i] == '\0') return 1;
    return 0;
}

/* A field must be non-empty. Colons and percents are allowed -- they are
 * escaped on the wire, which is what keeps the field separator unambiguous. */
static int wb_fed_field_ok(const char *s) {
    return s && *s;
}

/* Escape a field for the wire: '%' -> "%25", ':' -> "%3A". Everything else is
 * copied verbatim. Returns the escaped length, or 0 if it does not fit. */
static size_t wb_fed_escape(const char *in, char *out, size_t cap) {
    if (!in || !out || cap == 0) return 0;
    size_t w = 0;
    for (const char *p = in; *p; p++) {
        const char *rep = NULL;
        if (*p == '%') rep = "%25";
        else if (*p == ':') rep = "%3A";
        if (rep) {
            size_t l = strlen(rep);
            if (w + l + 1 > cap) return 0;
            memcpy(out + w, rep, l);
            w += l;
        } else {
            if (w + 2 > cap) return 0;
            out[w++] = *p;
        }
    }
    out[w] = '\0';
    return w;
}

static int wb_fed_hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Reverse wb_fed_escape. Returns the unescaped length, or -1 on a truncated or
 * non-hex escape, or if it does not fit. */
static int wb_fed_unescape(const char *in, size_t len, char *out, size_t cap) {
    if (!in || !out || cap == 0) return -1;
    size_t w = 0;
    for (size_t i = 0; i < len; i++) {
        char c = in[i];
        if (c == '%') {
            if (i + 2 >= len) return -1;
            int hi = wb_fed_hex(in[i + 1]), lo = wb_fed_hex(in[i + 2]);
            if (hi < 0 || lo < 0) return -1;
            if (w + 1 >= cap) return -1;
            out[w++] = (char)((hi << 4) | lo);
            i += 2;
        } else {
            if (w + 1 >= cap) return -1;
            out[w++] = c;
        }
    }
    out[w] = '\0';
    return (int)w;
}

int wb_fed_resource_match(const char *pattern, const char *resource) {
    if (!pattern || !resource) return 0;
    if (strcmp(pattern, "*") == 0) return 1;
    size_t plen = strlen(pattern);
    if (plen >= 2 && pattern[plen - 1] == '*' && pattern[plen - 2] == ':') {
        /* "prefix:*" matches "prefix" itself and anything under it */
        size_t prefix = plen - 2;
        if (strncmp(pattern, resource, prefix) != 0) return 0;
        return resource[prefix] == '\0' || resource[prefix] == ':';
    }
    return strcmp(pattern, resource) == 0;
}

size_t wb_fed_grant_encode(const wb_fed_grant *g, char *out, size_t cap) {
    if (!g || !out || cap == 0) return 0;
    if (!wb_fed_field_ok(g->server) || !wb_fed_field_ok(g->resource)) return 0;
    /* The grant owns fixed buffers, so a field that could never fit one must be
     * refused here -- otherwise it would encode and then fail to decode. */
    if (!wb_fed_fits(g->server, WB_FED_MAX_SERVER)) return 0;
    if (!wb_fed_fits(g->resource, WB_FED_MAX_RESOURCE)) return 0;

    char esc_server[WB_FED_MAX_STR], esc_resource[WB_FED_MAX_STR];
    if (!wb_fed_escape(g->server, esc_server, sizeof esc_server)) return 0;
    if (!wb_fed_escape(g->resource, esc_resource, sizeof esc_resource)) return 0;

    /* The checksum covers the body WITHOUT the prefix, and decode must agree --
     * the prefix is a constant, so hashing it would only invite the two sides
     * to disagree about where the body starts. */
    char body[WB_FED_MAX_STR * 2];
    int n = snprintf(body, sizeof body, "%s:%s:%u:%llu:%llu",
                     esc_server, esc_resource,
                     (unsigned)g->actions,
                     (unsigned long long)g->expiry,
                     (unsigned long long)g->nonce);
    if (n <= 0 || (size_t)n >= sizeof body) return 0;

    char full[WB_FED_MAX_STR * 2 + 16];
    n = snprintf(full, sizeof full, "%s%s:%08x", WB_FED_PREFIX, body,
                 (unsigned)(wb_fed_hash(body) & 0xFFFFFFFFu));
    if (n <= 0 || (size_t)n >= cap) return 0;
    memcpy(out, full, (size_t)n + 1);
    return (size_t)n;
}

int wb_fed_grant_decode(const char *s, wb_fed_grant *g) {
    if (!s || !g) return -1;
    memset(g, 0, sizeof *g);

    size_t plen = strlen(WB_FED_PREFIX);
    if (strncmp(s, WB_FED_PREFIX, plen) != 0) return -1;

    /* The checksum is the final field: "<body>:<8 hex>" */
    const char *last = strrchr(s, ':');
    if (!last || strlen(last + 1) != 8) return -1;
    for (int i = 1; i <= 8; i++) {
        char c = last[i];
        int ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!ok) return -1;
    }
    unsigned want = (unsigned)strtoul(last + 1, NULL, 16);

    /* The hashed body is everything between the prefix and the checksum field. */
    const char *body_start = s + plen;
    size_t body_len = (size_t)(last - body_start);
    char body[WB_FED_MAX_STR * 2];
    if (body_len + 1 >= sizeof body) return -1;
    memcpy(body, body_start, body_len);
    body[body_len] = '\0';
    if ((wb_fed_hash(body) & 0xFFFFFFFFu) != want) return -1;

    /* Five fields: server, resource, actions, expiry, nonce. They are copied
     * into the grant, which then owns them. */
    const char *fields[5];
    uint32_t lens[5];
    int nf = 0;
    const char *q = body;
    for (int i = 0; i < 5; i++) {
        const char *colon = (i == 4) ? (body + body_len) : strchr(q, ':');
        if (!colon) return -1;
        fields[nf] = q;
        lens[nf] = (uint32_t)(colon - q);
        nf++;
        q = colon + 1;
    }
    if (nf != 5) return -1;

    /* Unescape the two string fields. The colon is now unambiguous on the wire,
     * so the split above was safe; a field that is still empty after unescaping
     * is a malformed grant. */
    if (wb_fed_unescape(fields[0], lens[0], g->server, WB_FED_MAX_SERVER) <= 0) return -1;
    if (wb_fed_unescape(fields[1], lens[1], g->resource, WB_FED_MAX_RESOURCE) <= 0) return -1;
    /* strtoul stops at the ':' that follows each field, so the numeric fields
     * need no NUL of their own */
    g->actions = (uint32_t)strtoul(fields[2], NULL, 10);
    g->expiry  = strtoull(fields[3], NULL, 10);
    g->nonce   = strtoull(fields[4], NULL, 10);
    return 0;
}

int wb_fed_check(const wb_fed_grant *g, const char *resource, uint32_t action, uint64_t tick) {
    if (!g || !resource) return 0;
    if (g->expiry != 0 && tick > g->expiry) return 0;   /* expired */
    if ((g->actions & action) != action) return 0;      /* the action is not granted */
    return wb_fed_resource_match(g->resource, resource);
}

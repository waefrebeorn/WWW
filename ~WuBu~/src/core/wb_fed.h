/*
 * wb_fed.h -- federation capability grants (LP-56 stone 1).
 *
 * A foreign server should not need a shared database to be told what it may do
 * here. A grant is a self-describing, self-verifying string:
 *
 *   fed1:<server>:<resource>:<actions>:<expiry>:<nonce>:<checksum>
 *
 * The resource is a PATTERN -- "*" for anything, "world:*" for one world and its
 * children. Actions are bits. Expiry is a tick, 0 meaning never. The checksum is
 * FNV-1a over the fields that precede it, so a grant edited in transit, by a
 * client, or by a typo is rejected rather than silently reinterpreted.
 *
 * ':' and '%' in a field are percent-escaped on the wire ("%3A", "%25"), which
 * is what lets a resource pattern contain the very separator the format uses.
 *
 * The string is canonical: the same grant always encodes to the same bytes, so
 * two servers that both hold the grant agree on it without comparing structs.
 *
 * A decoded grant OWNS copies of its strings, so it stays valid after the string
 * it came from is gone.
 *
 * C11, no deps. Deterministic.
 */
#ifndef WB_FED_H
#define WB_FED_H

#include <stddef.h>
#include <stdint.h>

#define WB_FED_PREFIX "fed1:"
#define WB_FED_MAX_STR 512
#define WB_FED_MAX_SERVER 64
#define WB_FED_MAX_RESOURCE 128

/* Action bits. */
#define WB_FED_READ   (1u << 0)
#define WB_FED_WRITE  (1u << 1)
#define WB_FED_ADMIN  (1u << 2)
#define WB_FED_ALL    (WB_FED_READ | WB_FED_WRITE | WB_FED_ADMIN)

typedef struct {
    char     server[WB_FED_MAX_SERVER];
    char     resource[WB_FED_MAX_RESOURCE];
    uint32_t actions;      /* WB_FED_* bits */
    uint64_t expiry;       /* a tick; 0 = never */
    uint64_t nonce;        /* makes two otherwise-identical grants distinct */
} wb_fed_grant;

/* Encode a grant canonically. Returns the length written (excluding the NUL), or
 * 0 if the grant is unusable or the buffer is too small. */
size_t wb_fed_grant_encode(const wb_fed_grant *g, char *out, size_t cap);

/* Parse a canonical grant, copying its strings. Returns 0 on success, -1 if the
 * string is malformed, a field is too long, or the checksum does not match. */
int wb_fed_grant_decode(const char *s, wb_fed_grant *g);

/* 1 if the grant permits `action` on `resource` at `tick`. */
int wb_fed_check(const wb_fed_grant *g, const char *resource, uint32_t action, uint64_t tick);

/* 1 if `resource` matches the pattern. "*" matches anything; "prefix:*" matches
 * `prefix` and anything under it. */
int wb_fed_resource_match(const char *pattern, const char *resource);

#endif /* WB_FED_H */

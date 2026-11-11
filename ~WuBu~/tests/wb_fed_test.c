/*
 * wb_fed_test.c -- LP-56 stone 1 gate: federation capability grants.
 *
 * Devil's-advocate cases: a decoded grant OWNS its strings, so overwriting the
 * buffer it came from must not change it; a single edited character anywhere in
 * the grant is rejected by the checksum; a grant with a field too long for the
 * grant's own storage is rejected rather than truncated; and a wildcard pattern
 * that merely shares a prefix ("world:*" vs "worlds:x") does not match.
 */
#include "wb_fed.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static wb_fed_grant mk(const char *server, const char *resource, uint32_t actions,
                       uint64_t expiry, uint64_t nonce) {
    wb_fed_grant g;
    memset(&g, 0, sizeof g);
    snprintf(g.server, sizeof g.server, "%s", server);
    snprintf(g.resource, sizeof g.resource, "%s", resource);
    g.actions = actions;
    g.expiry = expiry;
    g.nonce = nonce;
    return g;
}

int main(void) {
    char s[WB_FED_MAX_STR];

    /* ---- round trip ---- */
    wb_fed_grant g = mk("alpha", "world:*", WB_FED_READ | WB_FED_WRITE, 1000, 42);
    size_t n = wb_fed_grant_encode(&g, s, sizeof s);
    CHECK(n > 0, "a grant encodes");
    CHECK(strncmp(s, WB_FED_PREFIX, strlen(WB_FED_PREFIX)) == 0, "the encoding carries the prefix");
    CHECK(strstr(s, "alpha") != NULL, "the server is in the string");
    CHECK(strstr(s, "world%3A*") != NULL, "the resource is escaped in the string");
    CHECK(strstr(s, ":42:") != NULL, "the nonce is in the string");

    wb_fed_grant d;
    CHECK(wb_fed_grant_decode(s, &d) == 0, "a canonical grant decodes");
    CHECK(strcmp(d.server, "alpha") == 0, "the server survives the round trip");
    CHECK(strcmp(d.resource, "world:*") == 0, "the resource survives the round trip");
    CHECK(d.actions == (WB_FED_READ | WB_FED_WRITE), "the actions survive the round trip");
    CHECK(d.expiry == 1000, "the expiry survives the round trip");
    CHECK(d.nonce == 42, "the nonce survives the round trip");

    /* ---- fields that need escaping round-trip ---- */
    {
        wb_fed_grant c = mk("a:b", "x:y:z", WB_FED_ALL, 5, 7);
        char cs[WB_FED_MAX_STR];
        CHECK(wb_fed_grant_encode(&c, cs, sizeof cs) > 0, "a colon-heavy grant encodes");
        CHECK(strchr(cs, ':') != NULL, "the wire still uses the separator");
        CHECK(strstr(cs, "%3A") != NULL, "the colons are escaped on the wire");
        wb_fed_grant cd;
        CHECK(wb_fed_grant_decode(cs, &cd) == 0, "a colon-heavy grant decodes");
        CHECK(strcmp(cd.server, "a:b") == 0, "a colon in the server round-trips");
        CHECK(strcmp(cd.resource, "x:y:z") == 0, "a colon in the resource round-trips");
    }
    {
        wb_fed_grant c = mk("srv", "a%3Ab", WB_FED_READ, 0, 1);
        char cs[WB_FED_MAX_STR];
        CHECK(wb_fed_grant_encode(&c, cs, sizeof cs) > 0, "an escape-looking resource encodes");
        CHECK(strstr(cs, "%253A") != NULL, "the percent is escaped so the sequence is not read as an escape");
        wb_fed_grant cd;
        CHECK(wb_fed_grant_decode(cs, &cd) == 0, "it decodes");
        CHECK(strcmp(cd.resource, "a%3Ab") == 0, "a literal escape sequence is not unescaped twice");
    }

    /* ---- encoding is canonical ---- */
    char s2[WB_FED_MAX_STR];
    wb_fed_grant_encode(&g, s2, sizeof s2);
    CHECK(strcmp(s, s2) == 0, "the same grant encodes to the same bytes");

    /* ---- the decoded grant owns its strings ---- */
    {
        char local[WB_FED_MAX_STR];
        wb_fed_grant_encode(&g, local, sizeof local);
        wb_fed_grant own;
        CHECK(wb_fed_grant_decode(local, &own) == 0, "a local grant decodes");
        memset(local, 'X', sizeof local);
        CHECK(strcmp(own.server, "alpha") == 0, "the server is copied, not pointed at");
        CHECK(strcmp(own.resource, "world:*") == 0, "the resource is copied, not pointed at");
    }

    /* ---- a tampered grant is rejected ---- */
    {
        char bad[WB_FED_MAX_STR];
        strcpy(bad, s);
        char *r = strstr(bad, "world%3A*");
        CHECK(r != NULL, "the escaped resource is present");
        r[0] = 'W';                                     /* one edited character */
        wb_fed_grant t;
        CHECK(wb_fed_grant_decode(bad, &t) == -1, "an edited character breaks the checksum");
    }
    {
        char bad[WB_FED_MAX_STR];
        strcpy(bad, s);
        char *a = strstr(bad, "alpha");
        a[0] = 'b';                                     /* the server, not the resource */
        wb_fed_grant t;
        CHECK(wb_fed_grant_decode(bad, &t) == -1, "editing the server breaks the checksum");
    }
    {
        char bad[WB_FED_MAX_STR];
        strcpy(bad, s);
        size_t len = strlen(bad);
        bad[len - 1] = (bad[len - 1] == '0') ? '1' : '0';   /* the checksum itself */
        wb_fed_grant t;
        CHECK(wb_fed_grant_decode(bad, &t) == -1, "editing the checksum breaks it");
    }

    /* ---- malformed grants are rejected ---- */
    wb_fed_grant t;
    CHECK(wb_fed_grant_decode("fed2:alpha:world:3:1000:42:00000000", &t) == -1, "a wrong prefix is rejected");
    CHECK(wb_fed_grant_decode("", &t) == -1, "an empty string is rejected");
    CHECK(wb_fed_grant_decode(NULL, &t) == -1, "a NULL string is rejected");
    CHECK(wb_fed_grant_decode(s, NULL) == -1, "a NULL grant is rejected");
    CHECK(wb_fed_grant_decode("fed1:alpha:world:3:1000:42", &t) == -1, "a missing checksum is rejected");
    CHECK(wb_fed_grant_decode("fed1:alpha:world:3:1000:42:zzzzzzzz", &t) == -1, "a non-hex checksum is rejected");
    CHECK(wb_fed_grant_decode("fed1:alpha:world:3:1000:42:abcdef", &t) == -1, "a short checksum is rejected");
    CHECK(wb_fed_grant_decode("fed1:alpha:world:3:1000:42:000000000", &t) == -1, "a long checksum is rejected");
    CHECK(wb_fed_grant_decode("fed1:alpha:world:3:1000:42:00000000:extra", &t) == -1, "an extra field is rejected");
    CHECK(wb_fed_grant_decode("fed1:world:3:1000:42:00000000", &t) == -1, "a missing field is rejected");

    /* ---- a field that fills its own storage is refused ---- */
    {
        wb_fed_grant big = mk("alpha", "*", WB_FED_READ, 0, 1);
        memset(big.server, 'a', WB_FED_MAX_SERVER);       /* 64 bytes, no NUL */
        CHECK(wb_fed_grant_encode(&big, s, sizeof s) == 0, "a server that fills its buffer is refused");
    }
    {
        wb_fed_grant big = mk("alpha", "*", WB_FED_READ, 0, 1);
        memset(big.resource, 'r', WB_FED_MAX_RESOURCE);   /* 128 bytes, no NUL */
        CHECK(wb_fed_grant_encode(&big, s, sizeof s) == 0, "a resource that fills its buffer is refused");
    }
    wb_fed_grant bad;
    bad = mk("", "*", WB_FED_READ, 0, 1);
    CHECK(wb_fed_grant_encode(&bad, s, sizeof s) == 0, "an empty server is refused");
    bad = mk("a:b", "*", WB_FED_READ, 0, 1);
    CHECK(wb_fed_grant_encode(&bad, s, sizeof s) > 0, "a colon in a field is escaped, not refused");
    bad = mk("alpha", "100%", WB_FED_READ, 0, 1);
    CHECK(wb_fed_grant_encode(&bad, s, sizeof s) > 0, "a percent in a field is escaped");
    bad = mk("alpha", "a%3Ab", WB_FED_READ, 0, 1);
    CHECK(wb_fed_grant_encode(&bad, s, sizeof s) > 0, "an escape sequence in a field is escaped");
    bad = mk("alpha", "", WB_FED_READ, 0, 1);
    CHECK(wb_fed_grant_encode(&bad, s, sizeof s) == 0, "an empty resource is refused");
    CHECK(wb_fed_grant_encode(&g, NULL, sizeof s) == 0, "a NULL output is refused");
    CHECK(wb_fed_grant_encode(&g, s, 8) == 0, "a small buffer is refused");

    /* ---- checking ---- */
    wb_fed_grant rw = mk("alpha", "world:*", WB_FED_READ | WB_FED_WRITE, 1000, 1);
    CHECK(wb_fed_check(&rw, "world:alpha", WB_FED_READ, 500) == 1, "a granted read is allowed");
    CHECK(wb_fed_check(&rw, "world:alpha", WB_FED_WRITE, 500) == 1, "a granted write is allowed");
    CHECK(wb_fed_check(&rw, "world:alpha", WB_FED_ADMIN, 500) == 0, "an ungranted action is refused");
    CHECK(wb_fed_check(&rw, "world:alpha", WB_FED_READ | WB_FED_ADMIN, 500) == 0, "a partly-granted pair is refused");
    CHECK(wb_fed_check(&rw, "other:beta", WB_FED_READ, 500) == 0, "an unmatched resource is refused");

    /* ---- expiry ---- */
    CHECK(wb_fed_check(&rw, "world:alpha", WB_FED_READ, 1000) == 1, "a grant is valid AT its expiry tick");
    CHECK(wb_fed_check(&rw, "world:alpha", WB_FED_READ, 1001) == 0, "a grant is refused one tick after expiry");
    CHECK(wb_fed_check(&rw, "world:alpha", WB_FED_READ, 999999) == 0, "a grant is refused long after expiry");
    wb_fed_grant forever = mk("alpha", "*", WB_FED_READ, 0, 1);
    CHECK(wb_fed_check(&forever, "anything", WB_FED_READ, 18446744073709551615ULL) == 1,
          "an expiry of 0 never expires");
    CHECK(wb_fed_check(NULL, "x", WB_FED_READ, 1) == 0, "a NULL grant refuses");
    CHECK(wb_fed_check(&rw, NULL, WB_FED_READ, 1) == 0, "a NULL resource refuses");

    /* ---- resource patterns ---- */
    CHECK(wb_fed_resource_match("*", "anything") == 1, "'*' matches anything");
    CHECK(wb_fed_resource_match("world:*", "world:alpha") == 1, "a prefix pattern matches a child");
    CHECK(wb_fed_resource_match("world:*", "world") == 1, "a prefix pattern matches the prefix itself");
    CHECK(wb_fed_resource_match("world:*", "world:alpha:deep") == 1, "a prefix pattern matches a grandchild");
    CHECK(wb_fed_resource_match("world:*", "worlds:x") == 0, "a prefix pattern does not match a longer name");
    CHECK(wb_fed_resource_match("world:*", "other:x") == 0, "a prefix pattern does not match elsewhere");
    CHECK(wb_fed_resource_match("exact", "exact") == 1, "an exact pattern matches exactly");
    CHECK(wb_fed_resource_match("exact", "exact2") == 0, "an exact pattern does not match a longer name");
    CHECK(wb_fed_resource_match(NULL, "x") == 0, "a NULL pattern matches nothing");
    CHECK(wb_fed_resource_match("*", NULL) == 0, "a NULL resource matches nothing");

    /* ---- determinism ---- */
    char da[WB_FED_MAX_STR], db[WB_FED_MAX_STR];
    wb_fed_grant ea = mk("srv", "a:*", WB_FED_ALL, 77, 9);
    wb_fed_grant eb = mk("srv", "a:*", WB_FED_ALL, 77, 9);
    wb_fed_grant_encode(&ea, da, sizeof da);
    wb_fed_grant_encode(&eb, db, sizeof db);
    CHECK(strcmp(da, db) == 0, "encoding is deterministic");
    wb_fed_grant pa, pb;
    wb_fed_grant_decode(da, &pa);
    wb_fed_grant_decode(db, &pb);
    CHECK(pa.actions == pb.actions && pa.expiry == pb.expiry && pa.nonce == pb.nonce &&
          strcmp(pa.server, pb.server) == 0 && strcmp(pa.resource, pb.resource) == 0,
          "decoding is deterministic");
    CHECK(wb_fed_check(&pa, "a:b", WB_FED_WRITE, 10) == wb_fed_check(&pb, "a:b", WB_FED_WRITE, 10),
          "checking is deterministic");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

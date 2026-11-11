/*
 * wb_prov_test.c -- LP-79 stone 1 gate: asset provenance and licensing.
 *
 * Devil's-advocate cases: the license is REQUIRED, so a perfectly well-formed
 * record with no license is refused; the allowlist is an ALLOWLIST, so a real
 * SPDX identifier that nobody added is refused rather than assumed fine; a
 * generated asset must name its model and carry a prompt hash, because without
 * them it cannot be audited or reproduced later; the credits are generated from
 * the same records that were checked, so they cannot drift; and the credits are
 * all-or-nothing, because a truncated credits file is worse than none.
 */
#include "wb_prov.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static wb_prov_rec mk(const char *path, wb_prov_source src, const char *lic,
                      const char *model, const char *prompt, const char *seed) {
    wb_prov_rec r;
    memset(&r, 0, sizeof r);
    if (path) snprintf(r.path, WB_PROV_NAME, "%s", path);
    r.source = src;
    if (lic) snprintf(r.license, WB_PROV_LICENSE, "%s", lic);
    if (model) snprintf(r.model, WB_PROV_MODEL, "%s", model);
    if (prompt) snprintf(r.prompt, WB_PROV_HASH, "%s", prompt);
    if (seed) snprintf(r.seed, WB_PROV_SEED, "%s", seed);
    return r;
}

static void build(wb_prov_registry *p) {
    wb_prov_init(p);
    wb_prov_allow(p, "CC0-1.0");
    wb_prov_allow(p, "CC-BY-4.0");
    wb_prov_allow(p, "MIT");
    wb_prov_allow(p, "Proprietary");
}

int main(void) {
    wb_prov_registry p;
    build(&p);

    /* ---- the allowlist ---- */
    CHECK(wb_prov_allow_count(&p) == 4, "four licenses are allowed");
    CHECK(wb_prov_is_allowed(&p, "MIT") == 1, "an allowed license is allowed");
    CHECK(wb_prov_is_allowed(&p, "GPL-3.0") == 0,
          "a real SPDX identifier nobody added is NOT allowed");
    CHECK(wb_prov_is_allowed(&p, "") == 0, "the empty string is not allowed");
    CHECK(wb_prov_is_allowed(&p, NULL) == 0, "a NULL license is not allowed");
    CHECK(wb_prov_is_allowed(NULL, "MIT") == 0, "a NULL registry allows nothing");
    CHECK(wb_prov_allow(&p, "MIT") == 0, "allowing a license twice is harmless");
    CHECK(wb_prov_allow_count(&p) == 4, "and it is not counted twice");
    CHECK(wb_prov_allow(&p, "") == -1, "an empty license is refused");
    CHECK(wb_prov_allow(&p, NULL) == -1, "a NULL license is refused");
    CHECK(wb_prov_allow(&p, "a-license-identifier-far-too-long-for-the-field") == -1,
          "an over-long license is refused");
    CHECK(wb_prov_allow(NULL, "X11") == -1, "a NULL registry refuses");

    /* ---- records that clear ---- */
    wb_prov_rec r = mk("earth.gltf", WB_PROV_AUTHORED, "CC-BY-4.0", NULL, NULL, NULL);
    CHECK(wb_prov_add(&p, &r) == 0, "an authored record is added");
    r = mk("prop.gltf", WB_PROV_IMPORTED, "MIT", NULL, NULL, NULL);
    CHECK(wb_prov_add(&p, &r) == 0, "an imported record is added");
    r = mk("gen/tree.gltf", WB_PROV_GENERATED, "CC0-1.0", "sd-xl", "9f2c", "42");
    CHECK(wb_prov_add(&p, &r) == 0, "a generated record is added");
    CHECK(wb_prov_count(&p) == 3, "three records are registered");

    uint32_t reason = WB_PROV_OK;
    CHECK(wb_prov_check(&p, "earth.gltf", &reason) == 0 && reason == WB_PROV_OK,
          "an authored asset clears");
    CHECK(wb_prov_check(&p, "prop.gltf", &reason) == 0 && reason == WB_PROV_OK,
          "an imported asset clears");
    CHECK(wb_prov_check(&p, "gen/tree.gltf", &reason) == 0 && reason == WB_PROV_OK,
          "a generated asset clears");
    CHECK(wb_prov_check(&p, "missing.gltf", &reason) == -1 &&
          reason == WB_PROV_ERR_UNKNOWN_ASSET,
          "an asset with no record is refused");

    const wb_prov_rec *f = wb_prov_find(&p, "prop.gltf");
    CHECK(f != NULL, "a record is found by path");
    CHECK(f != NULL && f->source == WB_PROV_IMPORTED, "and it keeps its source");
    CHECK(wb_prov_find(&p, "nope") == NULL, "an unknown path is not found");
    CHECK(wb_prov_find(&p, NULL) == NULL, "a NULL path is not found");
    CHECK(wb_prov_find(&p, "") == NULL, "an empty path is not found");
    CHECK(wb_prov_find(NULL, "prop.gltf") == NULL, "a NULL registry finds nothing");

    /* ---- the license is required ---- */
    r = mk("unlicensed.gltf", WB_PROV_AUTHORED, NULL, NULL, NULL, NULL);
    CHECK(wb_prov_add(&p, &r) == 0, "a record with no license can be recorded");
    CHECK(wb_prov_check(&p, "unlicensed.gltf", &reason) == -1 &&
          reason == WB_PROV_ERR_NO_LICENSE,
          "but it may not ship");
    CHECK(wb_prov_check_rec(&p, &r, &reason) == -1 &&
          reason == WB_PROV_ERR_NO_LICENSE,
          "a hand-built record with no license is refused too");

    /* ---- the allowlist is enforced ---- */
    r = mk("gpl.gltf", WB_PROV_IMPORTED, "GPL-3.0", NULL, NULL, NULL);
    CHECK(wb_prov_add(&p, &r) == 0, "an unallowed license can be recorded");
    CHECK(wb_prov_check(&p, "gpl.gltf", &reason) == -1 &&
          reason == WB_PROV_ERR_NOT_ALLOWED,
          "but it may not ship");

    /* ---- a generated asset must be auditable ---- */
    r = mk("gen/nomodel.gltf", WB_PROV_GENERATED, "CC0-1.0", NULL, "9f2c", NULL);
    CHECK(wb_prov_add(&p, &r) == -1, "a generated asset with no model is refused");
    r = mk("gen/noprompt.gltf", WB_PROV_GENERATED, "CC0-1.0", "sd-xl", NULL, NULL);
    CHECK(wb_prov_add(&p, &r) == -1, "a generated asset with no prompt is refused");
    CHECK(wb_prov_count(&p) == 5, "the refused records were not added");
    reason = WB_PROV_OK;
    r = mk("gen/nomodel.gltf", WB_PROV_GENERATED, "CC0-1.0", NULL, "9f2c", NULL);
    CHECK(wb_prov_check_rec(&p, &r, &reason) == -1 &&
          reason == WB_PROV_ERR_GENERATED_NO_MODEL,
          "a hand-built record with no model is refused with the right reason");
    r = mk("gen/noprompt.gltf", WB_PROV_GENERATED, "CC0-1.0", "sd-xl", NULL, NULL);
    CHECK(wb_prov_check_rec(&p, &r, &reason) == -1 &&
          reason == WB_PROV_ERR_GENERATED_NO_PROMPT,
          "and one with no prompt hash");
    CHECK(wb_prov_check_rec(NULL, &r, &reason) == -1 &&
          reason == WB_PROV_ERR_UNKNOWN_ASSET,
          "a NULL registry refuses a record as an unknown asset");
    CHECK(wb_prov_check(NULL, "earth.gltf", &reason) == -1,
          "and a path");

    /* ---- add refuses bad input ---- */
    CHECK(wb_prov_add(&p, NULL) == -1, "a NULL record is refused");
    r = mk("", WB_PROV_AUTHORED, "MIT", NULL, NULL, NULL);
    CHECK(wb_prov_add(&p, &r) == -1, "an empty path is refused");
    char longpath[WB_PROV_NAME];
    memset(longpath, 'x', WB_PROV_NAME - 1);
    longpath[WB_PROV_NAME - 1] = '\0';
    r = mk(longpath, WB_PROV_AUTHORED, "MIT", NULL, NULL, NULL);
    CHECK(wb_prov_add(&p, &r) == 0,
          "a path that exactly fills the field is accepted");
    r = mk("bad-source.gltf", (wb_prov_source)9, "MIT", NULL, NULL, NULL);
    CHECK(wb_prov_add(&p, &r) == -1, "an unknown source is refused");
    r = mk("earth.gltf", WB_PROV_IMPORTED, "MIT", NULL, NULL, NULL);
    CHECK(wb_prov_add(&p, &r) == -1, "a duplicate path is refused");
    CHECK(wb_prov_add(NULL, &r) == -1, "a NULL registry refuses a record");
    CHECK(wb_prov_count(&p) == 6, "the refused adds changed nothing");

    /* ---- the first refusal ---- */
    const wb_prov_rec *bad = wb_prov_first_refused(&p);
    CHECK(bad != NULL, "a registry with a bad record reports one");
    CHECK(bad != NULL && strcmp(bad->path, "unlicensed.gltf") == 0,
          "and it is the first one in insertion order");
    CHECK(wb_prov_first_refused(NULL) == NULL, "a NULL registry has none");

    /* ---- names ---- */
    CHECK(strcmp(wb_prov_source_name(WB_PROV_AUTHORED), "authored") == 0,
          "a source names itself");
    CHECK(strcmp(wb_prov_source_name(WB_PROV_IMPORTED), "imported") == 0,
          "the second source too");
    CHECK(strcmp(wb_prov_source_name(WB_PROV_GENERATED), "generated") == 0,
          "and the third");
    CHECK(strcmp(wb_prov_source_name(9), "?") == 0, "an unknown source has no name");
    CHECK(strcmp(wb_prov_reason_name(WB_PROV_OK), "ok") == 0, "a reason names itself");
    CHECK(strcmp(wb_prov_reason_name(WB_PROV_ERR_NOT_ALLOWED), "license not allowed") == 0,
          "the allowlist reason names itself");
    CHECK(strcmp(wb_prov_reason_name(99), "?") == 0, "an unknown reason has no name");

    /* ---- credits ---- */
    wb_prov_registry c;
    wb_prov_init(&c);
    wb_prov_allow(&c, "CC0-1.0");
    wb_prov_allow(&c, "CC-BY-4.0");
    r = mk("earth.gltf", WB_PROV_AUTHORED, "CC-BY-4.0", NULL, NULL, NULL);
    wb_prov_add(&c, &r);
    r = mk("gen/tree.gltf", WB_PROV_GENERATED, "CC0-1.0", "sd-xl", "9f2c", "42");
    wb_prov_add(&c, &r);
    r = mk("gen/rock.gltf", WB_PROV_GENERATED, "CC0-1.0", "sd-xl", "aa11", NULL);
    wb_prov_add(&c, &r);

    char out[512];
    size_t n = wb_prov_credits(&c, out, sizeof out);
    CHECK(n > 0, "credits are written");
    CHECK(strcmp(out,
                 "earth.gltf | CC-BY-4.0 | authored\n"
                 "gen/tree.gltf | CC0-1.0 | generated | model=sd-xl | prompt=9f2c | seed=42\n"
                 "gen/rock.gltf | CC0-1.0 | generated | model=sd-xl | prompt=aa11\n") == 0,
          "one line per record, in insertion order, with the audit trail");
    CHECK(n == strlen(out), "the return value is the byte count");

    char small[32];
    CHECK(wb_prov_credits(&c, small, sizeof small) == 0,
          "a buffer too small writes nothing");
    CHECK(small[0] == '\0', "and leaves the buffer empty");
    CHECK(wb_prov_credits(&c, NULL, sizeof out) == 0, "a NULL buffer is refused");
    CHECK(wb_prov_credits(&c, out, 0) == 0, "a zero capacity is refused");
    CHECK(wb_prov_credits(NULL, out, sizeof out) == 0, "a NULL registry is refused");

    wb_prov_registry empty;
    wb_prov_init(&empty);
    CHECK(wb_prov_credits(&empty, out, sizeof out) == 0,
          "an empty registry writes an empty credits block");
    CHECK(out[0] == '\0', "which is an empty string");

    /* ---- the allowlist has a ceiling ---- */
    wb_prov_registry full;
    wb_prov_init(&full);
    int allowed = 0;
    for (int i = 0; i < WB_PROV_MAX_ALLOW; i++) {
        char spdx[16];
        snprintf(spdx, sizeof spdx, "L%02d", i);
        if (wb_prov_allow(&full, spdx) == 0) allowed++;
    }
    CHECK(allowed == WB_PROV_MAX_ALLOW, "the allowlist fills to its ceiling");
    CHECK(wb_prov_allow(&full, "one-more") == -1, "and then refuses another");

    /* ---- the registry has a ceiling ---- */
    wb_prov_registry rfull;
    wb_prov_init(&rfull);
    wb_prov_allow(&rfull, "MIT");
    int added = 0;
    for (int i = 0; i < WB_PROV_MAX; i++) {
        char path[32];
        snprintf(path, sizeof path, "asset%03d.gltf", i);
        wb_prov_rec rr = mk(path, WB_PROV_AUTHORED, "MIT", NULL, NULL, NULL);
        if (wb_prov_add(&rfull, &rr) == 0) added++;
    }
    CHECK(added == WB_PROV_MAX, "records fill to the ceiling");
    r = mk("one-more.gltf", WB_PROV_AUTHORED, "MIT", NULL, NULL, NULL);
    CHECK(wb_prov_add(&rfull, &r) == -1, "and then an add is refused");
    CHECK(wb_prov_count(&rfull) == WB_PROV_MAX, "the registry is full");

    /* ---- degenerate input ---- */
    wb_prov_init(NULL);
    CHECK(wb_prov_count(NULL) == 0, "a NULL registry has no records");
    CHECK(wb_prov_allow_count(NULL) == 0, "and no allowlist");

    /* ---- determinism: the same records produce the same credits ---- */
    wb_prov_registry d1, d2;
    build(&d1);
    build(&d2);
    r = mk("earth.gltf", WB_PROV_AUTHORED, "CC-BY-4.0", NULL, NULL, NULL);
    wb_prov_add(&d1, &r);
    wb_prov_add(&d2, &r);
    r = mk("gen/tree.gltf", WB_PROV_GENERATED, "CC0-1.0", "sd-xl", "9f2c", "42");
    wb_prov_add(&d1, &r);
    wb_prov_add(&d2, &r);
    char o1[512], o2[512];
    CHECK(wb_prov_credits(&d1, o1, sizeof o1) == wb_prov_credits(&d2, o2, sizeof o2) &&
          strcmp(o1, o2) == 0,
          "two registries built the same way produce the same credits");
    CHECK(wb_prov_first_refused(&d1) == NULL &&
          wb_prov_first_refused(&d2) == NULL,
          "and both are clear");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

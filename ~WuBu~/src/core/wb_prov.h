/*
 * wb_prov.h -- asset provenance and licensing (LP-79 stone 1).
 *
 * The asset pipeline records what an asset IS (its content hash, in
 * wb_manifest) but not where it CAME FROM. That is a liability the moment two
 * kinds of producer share a world: a creator uploading their own work, and a
 * model generating something whose rights are not settled. So every asset
 * carries a provenance record, and the record is checked before the asset ships.
 *
 * A record names the asset, its SOURCE (authored, imported, generated), and an
 * SPDX license identifier. Three rules are enforced:
 *
 *   - the license is REQUIRED; an asset with none does not ship;
 *   - the license must be on the ALLOWLIST; anything else does not ship;
 *   - a GENERATED asset must name its model and carry a prompt hash, so it can be
 *     audited later -- a generated asset with no model cannot be traced to a
 *     version, and one with no prompt cannot be reproduced.
 *
 * The allowlist is explicit rather than a blocklist: a new license is refused
 * until someone adds it, which is the direction that makes an accidental
 * permissive default impossible.
 *
 * The same records generate the CREDITS, so the credits cannot drift from what
 * actually shipped.
 *
 * C11, no deps. Deterministic: records are reported in insertion order.
 */
#ifndef WB_PROV_H
#define WB_PROV_H

#include <stddef.h>
#include <stdint.h>

#define WB_PROV_MAX 64
#define WB_PROV_NAME 128
#define WB_PROV_LICENSE 32
#define WB_PROV_MODEL 64
#define WB_PROV_HASH 32
#define WB_PROV_SEED 32
#define WB_PROV_MAX_ALLOW 16

typedef enum {
    WB_PROV_AUTHORED = 0,   /* made by a person or an in-house tool */
    WB_PROV_IMPORTED = 1,   /* taken from a third party */
    WB_PROV_GENERATED = 2   /* produced by a model or a generator */
} wb_prov_source;

typedef enum {
    WB_PROV_OK = 0,
    WB_PROV_ERR_UNKNOWN_ASSET,        /* no record for that path */
    WB_PROV_ERR_NO_LICENSE,           /* the record carries no license */
    WB_PROV_ERR_NOT_ALLOWED,          /* the license is not on the allowlist */
    WB_PROV_ERR_GENERATED_NO_MODEL,   /* a generated asset must name its model */
    WB_PROV_ERR_GENERATED_NO_PROMPT   /* a generated asset must carry a prompt hash */
} wb_prov_reason;

typedef struct {
    char     path[WB_PROV_NAME];
    uint32_t source;                  /* a wb_prov_source */
    char     license[WB_PROV_LICENSE];/* an SPDX identifier */
    char     model[WB_PROV_MODEL];    /* generated only: the model id */
    char     prompt[WB_PROV_HASH];    /* generated only: the prompt hash */
    char     seed[WB_PROV_SEED];      /* generated only: the seed, if any */
} wb_prov_rec;

typedef struct {
    wb_prov_rec recs[WB_PROV_MAX];
    uint32_t    n;
    char        allow[WB_PROV_MAX_ALLOW][WB_PROV_LICENSE];
    uint32_t    nallow;
} wb_prov_registry;

void wb_prov_init(wb_prov_registry *p);
uint32_t wb_prov_count(const wb_prov_registry *p);
uint32_t wb_prov_allow_count(const wb_prov_registry *p);

/* Add a license to the allowlist. Returns 0 on success, -1 if refused. */
int  wb_prov_allow(wb_prov_registry *p, const char *spdx);
int  wb_prov_is_allowed(const wb_prov_registry *p, const char *spdx);

/* Add a record. Returns 0 on success, -1 if refused: a NULL or empty path, a path
 * or string that does not fit, an unknown source, a duplicate path, or a
 * generated asset with no model or no prompt hash. */
int  wb_prov_add(wb_prov_registry *p, const wb_prov_rec *rec);

const wb_prov_rec *wb_prov_find(const wb_prov_registry *p, const char *path);

/* Check one asset. Returns 0 if it may ship, -1 if it may not, with `reason`
 * receiving a wb_prov_reason. */
int wb_prov_check(const wb_prov_registry *p, const char *path, uint32_t *reason);
int wb_prov_check_rec(const wb_prov_registry *p, const wb_prov_rec *rec, uint32_t *reason);

/* The first record that may not ship, or NULL if every record is clear. */
const wb_prov_rec *wb_prov_first_refused(const wb_prov_registry *p);

/* The name of a source or reason, for a report. */
const char *wb_prov_source_name(uint32_t source);
const char *wb_prov_reason_name(uint32_t reason);

/* Write the credits -- one line per record, in insertion order -- as
 * "<path> | <license> | <source>[ | model=<model> | prompt=<prompt> | seed=<seed>]".
 * All or nothing: returns the bytes written, or 0 if the buffer is too small or
 * an argument is bad. */
size_t wb_prov_credits(const wb_prov_registry *p, char *out, size_t cap);

#endif /* WB_PROV_H */

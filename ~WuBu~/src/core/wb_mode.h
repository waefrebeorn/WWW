/*
 * wb_mode.h -- WuBu Engine gametype manifest + config schema.
 *
 * The UEFN "project" analogue, minus the platform. A gametype is a declared,
 * parameterisable package: a manifest says what it is and what it needs, and
 * the space instantiates it. The engine does not know what any particular
 * gametype *means* -- that ignorance is what lets all of them work.
 *
 *   gametype/
 *   ├── mode.json      name, version, entry script, config schema, teams
 *   ├── main.lua       the mode's step() -- owns gameplay
 *   └── assets/        referenced by path
 *
 * C11, no external deps beyond wb_json + wb_util.
 */
#ifndef WB_MODE_H
#define WB_MODE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WB_MODE_NAME_LEN    128
#define WB_MODE_VERSION_LEN 32
#define WB_MODE_AUTHOR_LEN  64
#define WB_MODE_ENTRY_LEN   256
#define WB_MODE_DESC_LEN    512
#define WB_MODE_CFG_NAME    64
#define WB_MODE_CFG_VALUE   128
#define WB_MODE_MAX_CFG     32

typedef enum {
    WB_MODE_CFG_INT = 0,
    WB_MODE_CFG_FLOAT,
    WB_MODE_CFG_BOOL,
    WB_MODE_CFG_STRING
} wb_mode_cfg_type;

typedef struct {
    char name[WB_MODE_CFG_NAME];
    wb_mode_cfg_type type;
    double min_val, max_val;   /* numeric range; ignored for bool/string */
    char dflt[WB_MODE_CFG_VALUE];
    int required;              /* 1 = must be supplied by the caller */
} wb_mode_cfg_desc;

typedef struct {
    char name[WB_MODE_NAME_LEN];
    char version[WB_MODE_VERSION_LEN];
    char author[WB_MODE_AUTHOR_LEN];
    char entry[WB_MODE_ENTRY_LEN];       /* entry script, relative to the mode dir */
    char description[WB_MODE_DESC_LEN];
    int  max_players;
    int  teams;
    wb_mode_cfg_desc cfgs[WB_MODE_MAX_CFG];
    int  ncfgs;
    char error[512];
} wb_mode_manifest;

/* Load and parse a mode.json. Returns 0 on success; on failure out->error is
 * filled and the rest of the manifest is zeroed except the defaults. */
int wb_mode_manifest_load(const char *path, wb_mode_manifest *out);

/* Check a manifest is usable. Returns 0 when it is; `err` (if not NULL)
 * receives the reason. */
int wb_mode_manifest_validate(const wb_mode_manifest *m, char *err, size_t err_sz);

/* Resolve the entry script against the mode's directory. */
int wb_mode_entry_path(const wb_mode_manifest *m, const char *mode_dir,
                       char *out, size_t out_sz);

/* ------------------------------------------------------------- config */

typedef struct {
    char name[WB_MODE_CFG_NAME];
    char value[WB_MODE_CFG_VALUE];
} wb_mode_cfg_value;

#define WB_MODE_MAX_CFG_VALUES 32

typedef struct {
    wb_mode_cfg_value values[WB_MODE_MAX_CFG_VALUES];
    int nvalues;
} wb_mode_config;

/* Check every value against the schema: unknown keys, missing required keys,
 * unparseable values, out-of-range numerics. Returns 0 when valid. */
int wb_mode_config_validate(const wb_mode_manifest *m, const wb_mode_config *c,
                            char *err, size_t err_sz);

/* Typed accessors: the schema default when the key is absent. */
int    wb_mode_config_get_int(const wb_mode_manifest *m, const wb_mode_config *c,
                              const char *name, int dflt);
double wb_mode_config_get_float(const wb_mode_manifest *m, const wb_mode_config *c,
                                const char *name, double dflt);
int    wb_mode_config_get_bool(const wb_mode_manifest *m, const wb_mode_config *c,
                               const char *name, int dflt);

/* Fill `out` with every schema default -- the "no config supplied" case. */
void wb_mode_config_defaults(const wb_mode_manifest *m, wb_mode_config *out);

#ifdef __cplusplus
}
#endif

#endif /* WB_MODE_H */

/*
 * wb_mode.c -- gametype manifest + config schema. See wb_mode.h.
 */
#include "wb_mode.h"
#include "wb_json.h"
#include "wb_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *kTypeNames[] = { "int", "float", "bool", "string" };

static int parse_type(const char *s, wb_mode_cfg_type *out) {
    if (!s) return -1;
    for (int i = 0; i < 4; i++) {
        if (!strcmp(s, kTypeNames[i])) { *out = (wb_mode_cfg_type)i; return 0; }
    }
    return -1;
}

int wb_mode_manifest_load(const char *path, wb_mode_manifest *out) {
    memset(out, 0, sizeof *out);
    out->max_players = 32;          /* sensible floor */
    out->teams = 1;

    size_t len = 0;
    unsigned char *file = wb_read_file(path, &len);
    if (!file) {
        snprintf(out->error, sizeof out->error, "cannot read %s", path);
        return -1;
    }
    wb_json *root = wb_json_parse((const char *)file, len);
    free(file);
    if (!root) {
        snprintf(out->error, sizeof out->error, "JSON parse failed in %s", path);
        return -1;
    }

    const char *name = wb_json_str(wb_json_get(root, "name"));
    const char *ver  = wb_json_str(wb_json_get(root, "version"));
    const char *auth = wb_json_str(wb_json_get(root, "author"));
    const char *ent  = wb_json_str(wb_json_get(root, "entry"));
    const char *desc = wb_json_str(wb_json_get(root, "description"));

    if (name) snprintf(out->name, sizeof out->name, "%s", name);
    if (ver)  snprintf(out->version, sizeof out->version, "%s", ver);
    if (auth) snprintf(out->author, sizeof out->author, "%s", auth);
    if (ent)  snprintf(out->entry, sizeof out->entry, "%s", ent);
    if (desc) snprintf(out->description, sizeof out->description, "%s", desc);

    out->max_players = wb_json_int(wb_json_get(root, "max_players"), 32);
    out->teams      = wb_json_int(wb_json_get(root, "teams"), 1);

    /* Declared spawn points. These exist before any script runs, which is what
     * lets one mode travel INTO another: the destination's landing points must
     * already be there. */
    wb_json *spawns = wb_json_get(root, "spawns");
    if (spawns && spawns->type == WB_JSON_ARR) {
        for (int i = 0; i < spawns->n && out->nspawns < WB_MODE_MAX_SPAWNS; i++) {
            wb_json *s = spawns->items[i];
            wb_mode_spawn *d = &out->spawns[out->nspawns];
            memset(d, 0, sizeof *d);

            wb_json *p = wb_json_get(s, "pos");
            if (p && p->type == WB_JSON_ARR && p->n >= 3) {
                d->pos[0] = (float)wb_json_num(p->items[0], 0.0);
                d->pos[1] = (float)wb_json_num(p->items[1], 0.0);
                d->pos[2] = (float)wb_json_num(p->items[2], 0.0);
            }
            d->team = wb_json_int(wb_json_get(s, "team"), -1);
            const char *tag = wb_json_str(wb_json_get(s, "tag"));
            if (tag) snprintf(d->tag, sizeof d->tag, "%s", tag);
            out->nspawns++;
        }
    }

    wb_json *cfgs = wb_json_get(root, "config");
    if (cfgs && cfgs->type == WB_JSON_ARR) {
        for (int i = 0; i < cfgs->n && out->ncfgs < WB_MODE_MAX_CFG; i++) {
            wb_json *c = cfgs->items[i];
            wb_mode_cfg_desc *d = &out->cfgs[out->ncfgs];
            memset(d, 0, sizeof *d);

            const char *cn = wb_json_str(wb_json_get(c, "name"));
            if (!cn) continue;
            snprintf(d->name, sizeof d->name, "%s", cn);

            if (parse_type(wb_json_str(wb_json_get(c, "type")), &d->type) != 0) {
                snprintf(out->error, sizeof out->error,
                         "config %s: unknown type", d->name);
                wb_json_free(root);
                return -1;
            }
            d->min_val = wb_json_num(wb_json_get(c, "min"), 0.0);
            d->max_val = wb_json_num(wb_json_get(c, "max"), 0.0);
            const char *dv = wb_json_str(wb_json_get(c, "default"));
            if (dv) snprintf(d->dflt, sizeof d->dflt, "%s", dv);
            d->required = wb_json_int(wb_json_get(c, "required"), 0);
            out->ncfgs++;
        }
    }

    wb_json_free(root);
    return 0;
}

int wb_mode_manifest_validate(const wb_mode_manifest *m, char *err, size_t err_sz) {
    if (!m) {
        if (err) snprintf(err, err_sz, "null manifest");
        return -1;
    }
    if (!m->name[0]) {
        if (err) snprintf(err, err_sz, "manifest has no name");
        return -1;
    }
    if (!m->entry[0]) {
        if (err) snprintf(err, err_sz, "manifest '%s' has no entry script", m->name);
        return -1;
    }
    if (m->teams < 1) {
        if (err) snprintf(err, err_sz, "manifest '%s': teams must be >= 1", m->name);
        return -1;
    }
    if (m->max_players < 1) {
        if (err) snprintf(err, err_sz, "manifest '%s': max_players must be >= 1", m->name);
        return -1;
    }
    /* Every numeric config needs a range that actually contains its default. */
    for (int i = 0; i < m->ncfgs; i++) {
        const wb_mode_cfg_desc *d = &m->cfgs[i];
        if (d->type == WB_MODE_CFG_BOOL || d->type == WB_MODE_CFG_STRING) continue;
        if (d->max_val < d->min_val) {
            if (err) snprintf(err, err_sz,
                              "config '%s': max < min", d->name);
            return -1;
        }
    }
    return 0;
}

int wb_mode_entry_path(const wb_mode_manifest *m, const char *mode_dir,
                       char *out, size_t out_sz) {
    if (!m || !out || out_sz == 0) return -1;
    return wb_join_path(out, out_sz, mode_dir, m->entry);
}

/* ------------------------------------------------------------- config */

static const wb_mode_cfg_value *find_value(const wb_mode_config *c, const char *name) {
    if (!c || !name) return NULL;
    for (int i = 0; i < c->nvalues; i++)
        if (!strcmp(c->values[i].name, name)) return &c->values[i];
    return NULL;
}

static const wb_mode_cfg_desc *find_desc(const wb_mode_manifest *m, const char *name) {
    if (!m || !name) return NULL;
    for (int i = 0; i < m->ncfgs; i++)
        if (!strcmp(m->cfgs[i].name, name)) return &m->cfgs[i];
    return NULL;
}

static int parse_double(const char *s, double *out) {
    if (!s || !s[0]) return -1;
    char *end = NULL;
    double v = strtod(s, &end);
    if (end == s || *end != 0) return -1;
    *out = v;
    return 0;
}

static int parse_bool(const char *s, int *out) {
    if (!s) return -1;
    if (!strcmp(s, "true") || !strcmp(s, "1") || !strcmp(s, "yes"))  { *out = 1; return 0; }
    if (!strcmp(s, "false") || !strcmp(s, "0") || !strcmp(s, "no")) { *out = 0; return 0; }
    return -1;
}

int wb_mode_config_validate(const wb_mode_manifest *m, const wb_mode_config *c,
                            char *err, size_t err_sz) {
    if (!m) { if (err) snprintf(err, err_sz, "null manifest"); return -1; }

    /* unknown keys */
    for (int i = 0; i < c->nvalues; i++) {
        if (!find_desc(m, c->values[i].name)) {
            if (err) snprintf(err, err_sz, "unknown config key '%s'", c->values[i].name);
            return -1;
        }
    }

    /* every schema entry: required present, value parseable and in range */
    for (int i = 0; i < m->ncfgs; i++) {
        const wb_mode_cfg_desc *d = &m->cfgs[i];
        const wb_mode_cfg_value *v = find_value(c, d->name);
        const char *raw = v ? v->value : NULL;

        if (!raw) {
            if (d->required) {
                if (err) snprintf(err, err_sz, "missing required config '%s'", d->name);
                return -1;
            }
            continue;   /* absent + not required -> schema default applies */
        }

        if (d->type == WB_MODE_CFG_INT || d->type == WB_MODE_CFG_FLOAT) {
            double num;
            if (parse_double(raw, &num) != 0) {
                if (err) snprintf(err, err_sz, "config '%s': '%s' is not a number",
                                  d->name, raw);
                return -1;
            }
            if (num < d->min_val || num > d->max_val) {
                if (err) snprintf(err, err_sz,
                                  "config '%s': %g outside [%g, %g]",
                                  d->name, num, d->min_val, d->max_val);
                return -1;
            }
        } else if (d->type == WB_MODE_CFG_BOOL) {
            int b;
            if (parse_bool(raw, &b) != 0) {
                if (err) snprintf(err, err_sz,
                                  "config '%s': '%s' is not a bool (true/false/1/0)",
                                  d->name, raw);
                return -1;
            }
        }
    }
    return 0;
}

int wb_mode_config_get_int(const wb_mode_manifest *m, const wb_mode_config *c,
                           const char *name, int dflt) {
    const wb_mode_cfg_desc *d = find_desc(m, name);
    const wb_mode_cfg_value *v = find_value(c, name);
    if (!v) {
        if (!d || d->type != WB_MODE_CFG_INT) return dflt;
        return (int)strtod(d->dflt, NULL);
    }
    return (int)strtod(v->value, NULL);
}

double wb_mode_config_get_float(const wb_mode_manifest *m, const wb_mode_config *c,
                                 const char *name, double dflt) {
    const wb_mode_cfg_desc *d = find_desc(m, name);
    const wb_mode_cfg_value *v = find_value(c, name);
    if (!v) {
        if (!d || d->type != WB_MODE_CFG_FLOAT) return dflt;
        return strtod(d->dflt, NULL);
    }
    return strtod(v->value, NULL);
}

int wb_mode_config_get_bool(const wb_mode_manifest *m, const wb_mode_config *c,
                            const char *name, int dflt) {
    const wb_mode_cfg_desc *d = find_desc(m, name);
    const wb_mode_cfg_value *v = find_value(c, name);
    const char *raw = v ? v->value : (d ? d->dflt : NULL);
    int b;
    if (!raw || parse_bool(raw, &b) != 0) return dflt;
    return b;
}

void wb_mode_config_defaults(const wb_mode_manifest *m, wb_mode_config *out) {
    memset(out, 0, sizeof *out);
    if (!m) return;
    for (int i = 0; i < m->ncfgs && out->nvalues < WB_MODE_MAX_CFG_VALUES; i++) {
        snprintf(out->values[out->nvalues].name,
                 sizeof out->values[out->nvalues].name, "%s", m->cfgs[i].name);
        snprintf(out->values[out->nvalues].value,
                 sizeof out->values[out->nvalues].value, "%s", m->cfgs[i].dflt);
        out->nvalues++;
    }
}

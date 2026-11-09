/*
 * wb_json.c -- minimal JSON parser. See wb_json.h.
 */
#include "wb_json.h"

#include <stdlib.h>
#include <string.h>

typedef struct { const char *s; size_t i, len; } jparser;

static void jskip(jparser *p) {
    while (p->i < p->len) {
        char c = p->s[p->i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') p->i++;
        else break;
    }
}

static wb_json *jnew(wb_json_type t) {
    wb_json *v = (wb_json *)calloc(1, sizeof *v);
    v->type = t;
    return v;
}

void wb_json_free(wb_json *v) {
    if (!v) return;
    free(v->str);
    for (int i = 0; i < v->n; i++) wb_json_free(v->items[i]);
    /* only objects own keys; arrays must never free an uninitialised block */
    if (v->type == WB_JSON_OBJ) {
        for (int i = 0; i < v->n; i++) free(v->keys[i]);
        free(v->keys);
    }
    free(v->items);
    free(v);
}

static wb_json *jparse_value(jparser *p);

static char *jparse_string_raw(jparser *p) {
    if (p->i >= p->len || p->s[p->i] != '"') return NULL;
    p->i++;
    size_t cap = 32, n = 0;
    char *out = (char *)malloc(cap);
    while (p->i < p->len) {
        char c = p->s[p->i++];
        if (c == '"') { out[n] = 0; return out; }
        if (c == '\\' && p->i < p->len) {
            char e = p->s[p->i++];
            switch (e) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case '/': c = '/';  break;
                case '\\': c = '\\'; break;
                case '"': c = '"';  break;
                case 'u': {
                    if (p->i + 4 > p->len) break;
                    unsigned cp = 0;
                    for (int k = 0; k < 4; k++) {
                        char h = p->s[p->i++];
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                    }
                    if (n + 4 >= cap) { cap *= 2; out = (char *)realloc(out, cap); }
                    if (cp < 0x80) out[n++] = (char)cp;
                    else if (cp < 0x800) {
                        out[n++] = (char)(0xC0 | (cp >> 6));
                        out[n++] = (char)(0x80 | (cp & 0x3F));
                    } else {
                        out[n++] = (char)(0xE0 | (cp >> 12));
                        out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        out[n++] = (char)(0x80 | (cp & 0x3F));
                    }
                    continue;
                }
                default: c = e; break;
            }
        }
        if (n + 1 >= cap) { cap *= 2; out = (char *)realloc(out, cap); }
        out[n++] = c;
    }
    free(out);
    return NULL;
}

static wb_json *jparse_object(jparser *p) {
    wb_json *v = jnew(WB_JSON_OBJ);
    p->i++;  /* { */
    jskip(p);
    if (p->i < p->len && p->s[p->i] == '}') { p->i++; return v; }
    for (;;) {
        jskip(p);
        char *key = jparse_string_raw(p);
        if (!key) { wb_json_free(v); return NULL; }
        jskip(p);
        if (p->i >= p->len || p->s[p->i] != ':') { free(key); wb_json_free(v); return NULL; }
        p->i++;
        jskip(p);
        wb_json *val = jparse_value(p);
        if (!val) { free(key); wb_json_free(v); return NULL; }
        v->items = (wb_json **)realloc(v->items, (size_t)(v->n + 1) * sizeof(wb_json *));
        v->keys  = (char **)realloc(v->keys, (size_t)(v->n + 1) * sizeof(char *));
        v->items[v->n] = val;
        v->keys[v->n] = key;
        v->n++;
        jskip(p);
        if (p->i < p->len && p->s[p->i] == ',') { p->i++; continue; }
        if (p->i < p->len && p->s[p->i] == '}') { p->i++; return v; }
        wb_json_free(v);
        return NULL;
    }
}

static wb_json *jparse_array(jparser *p) {
    wb_json *v = jnew(WB_JSON_ARR);
    p->i++;  /* [ */
    jskip(p);
    if (p->i < p->len && p->s[p->i] == ']') { p->i++; return v; }
    for (;;) {
        jskip(p);
        wb_json *val = jparse_value(p);
        if (!val) { wb_json_free(v); return NULL; }
        v->items = (wb_json **)realloc(v->items, (size_t)(v->n + 1) * sizeof(wb_json *));
        v->items[v->n] = val;
        v->n++;
        jskip(p);
        if (p->i < p->len && p->s[p->i] == ',') { p->i++; continue; }
        if (p->i < p->len && p->s[p->i] == ']') { p->i++; return v; }
        wb_json_free(v);
        return NULL;
    }
}

static wb_json *jparse_value(jparser *p) {
    jskip(p);
    if (p->i >= p->len) return NULL;
    char c = p->s[p->i];
    if (c == '{') return jparse_object(p);
    if (c == '[') return jparse_array(p);
    if (c == '"') {
        wb_json *v = jnew(WB_JSON_STR);
        v->str = jparse_string_raw(p);
        if (!v->str) { free(v); return NULL; }
        return v;
    }
    if (!strncmp(p->s + p->i, "true", 4))  { p->i += 4; wb_json *v = jnew(WB_JSON_BOOL); v->boolean = 1; return v; }
    if (!strncmp(p->s + p->i, "false", 5)) { p->i += 5; wb_json *v = jnew(WB_JSON_BOOL); v->boolean = 0; return v; }
    if (!strncmp(p->s + p->i, "null", 4))  { p->i += 4; return jnew(WB_JSON_NULL); }
    {
        char *end = NULL;
        double d = strtod(p->s + p->i, &end);
        if (end == p->s + p->i) return NULL;
        p->i = (size_t)(end - p->s);
        wb_json *v = jnew(WB_JSON_NUM);
        v->num = d;
        return v;
    }
}

wb_json *wb_json_parse(const char *text, size_t len) {
    jparser p = { text, 0, len };
    return jparse_value(&p);
}

wb_json *wb_json_get(const wb_json *obj, const char *key) {
    if (!obj || obj->type != WB_JSON_OBJ) return NULL;
    for (int i = 0; i < obj->n; i++)
        if (obj->keys[i] && !strcmp(obj->keys[i], key)) return obj->items[i];
    return NULL;
}

int wb_json_int(const wb_json *v, int dflt) {
    if (!v) return dflt;
    if (v->type == WB_JSON_NUM) return (int)v->num;
    /* JSON booleans coerce to 1/0. Without this, "required": true reads as
     * the default and every such flag in every manifest is silently off. */
    if (v->type == WB_JSON_BOOL) return v->boolean ? 1 : 0;
    return dflt;
}

double wb_json_num(const wb_json *v, double dflt) {
    return (v && v->type == WB_JSON_NUM) ? v->num : dflt;
}

const char *wb_json_str(const wb_json *v) {
    return (v && v->type == WB_JSON_STR) ? v->str : NULL;
}

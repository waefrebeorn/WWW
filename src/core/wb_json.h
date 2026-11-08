/*
 * wb_json.h -- minimal JSON parser shared by the content pipeline.
 *
 * Objects, arrays, strings (with \uXXXX decoding), numbers, booleans and null.
 * Deliberately small: glTF and the manifest are the only consumers, and a
 * dependency-free parser keeps the no-third-party pillar intact.
 */
#ifndef WB_JSON_H
#define WB_JSON_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { WB_JSON_NULL, WB_JSON_BOOL, WB_JSON_NUM, WB_JSON_STR,
               WB_JSON_ARR, WB_JSON_OBJ } wb_json_type;

typedef struct wb_json {
    wb_json_type type;
    double       num;
    int          boolean;
    char        *str;
    struct wb_json **items;
    char        **keys;
    int          n;
} wb_json;

/* Parse `text` (length `len`, need not be NUL-terminated). Returns NULL on
 * error. Free with wb_json_free. */
wb_json *wb_json_parse(const char *text, size_t len);
void     wb_json_free(wb_json *v);

/* Object lookup by key, or NULL. */
wb_json *wb_json_get(const wb_json *obj, const char *key);

/* Typed accessors with defaults. */
int         wb_json_int(const wb_json *v, int dflt);
double      wb_json_num(const wb_json *v, double dflt);
const char *wb_json_str(const wb_json *v);

#ifdef __cplusplus
}
#endif

#endif /* WB_JSON_H */

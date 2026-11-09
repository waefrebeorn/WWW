/*
 * wb_util.c -- WuBu Engine shared C11 utilities. See wb_util.h.
 */
#include "wb_util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

unsigned char *wb_read_file(const char *path, size_t *out_len) {
    if (out_len) *out_len = 0;
    if (!path) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    unsigned char *b = (unsigned char *)malloc((size_t)n + 1);
    if (!b) { fclose(f); return NULL; }
    size_t got = fread(b, 1, (size_t)n, f);
    fclose(f);
    b[got] = 0;
    if (out_len) *out_len = got;
    return b;
}

int wb_file_exists(const char *path) {
    if (!path) return 0;
    struct stat st;
    return stat(path, &st) == 0;
}

int wb_ensure_dir(const char *path) {
    if (!path || !path[0]) return 0;
    if (wb_file_exists(path)) return 0;
#ifdef _WIN32
    return _mkdir(path) == 0 ? 0 : -1;
#else
    return mkdir(path, 0755) == 0 ? 0 : -1;
#endif
}

int wb_join_path(char *out, size_t out_sz, const char *dir, const char *name) {
    if (!out || out_sz == 0) return -1;
    out[0] = 0;
    if (!name) return -1;
    if (dir && dir[0]) snprintf(out, out_sz, "%s/%s", dir, name);
    else snprintf(out, out_sz, "%s", name);
    return 0;
}

void wb_set_err(char *buf, size_t buf_sz, const char *fmt, ...) {
    if (!buf || buf_sz == 0) return;
    buf[0] = 0;
    if (!fmt) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, buf_sz, fmt, ap);
    va_end(ap);
}

uint64_t wb_fnv1a(const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < len; i++) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

int wb_hex64(uint64_t h, char out[17]) {
    if (!out) return -1;
    snprintf(out, 17, "%016llx", (unsigned long long)h);
    return 0;
}

int wb_content_hash(const char *path, char out[17]) {
    if (!out) return -1;
    out[0] = 0;
    size_t len = 0;
    unsigned char *b = wb_read_file(path, &len);
    if (!b) return -1;
    int rc = wb_hex64(wb_fnv1a(b, len), out);
    free(b);
    return rc;
}

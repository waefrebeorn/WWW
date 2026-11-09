/*
 * wb_util.h -- WuBu Engine shared C11 utilities.
 *
 * The one place for the helpers that several modules otherwise re-implement:
 * file IO, directory creation, path joining, the module error buffer, and
 * FNV-1a content hashing. Every module that needs them includes this header;
 * no module rolls its own copy.
 *
 * C11, no external deps.
 */
#ifndef WB_UTIL_H
#define WB_UTIL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Read a whole file into a NUL-terminated malloc'd buffer. Returns NULL on
 * failure; `*out_len` (when out_len is not NULL) receives the byte count. */
unsigned char *wb_read_file(const char *path, size_t *out_len);

int wb_file_exists(const char *path);

/* Create one directory level if it does not exist. 0 on success. */
int wb_ensure_dir(const char *path);

/* Join `dir` and `name` with a single '/'. An empty/NULL dir yields `name`. */
int wb_join_path(char *out, size_t out_sz, const char *dir, const char *name);

/* Format into a caller-owned module error buffer (the wb_*_error() pattern).
 * Always NUL-terminates. */
void wb_set_err(char *buf, size_t buf_sz, const char *fmt, ...);

/* FNV-1a 64-bit over `len` bytes. */
uint64_t wb_fnv1a(const void *data, size_t len);

/* Render a 64-bit hash as 16 lowercase hex chars + NUL into out[17]. */
int wb_hex64(uint64_t h, char out[17]);

/* Content hash of a file, as 16 hex chars + NUL. Returns 0 on success, -1 if
 * the file cannot be read (out is then empty). */
int wb_content_hash(const char *path, char out[17]);

#ifdef __cplusplus
}
#endif

#endif /* WB_UTIL_H */

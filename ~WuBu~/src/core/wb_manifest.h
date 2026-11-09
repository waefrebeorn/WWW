/*
 * wb_manifest.h -- WuBu Engine asset manifest.
 *
 * A manifest records the content hash of every shipped asset, so a client can
 * verify that what it has on disk is what the world expects -- the integrity
 * half of the content pipeline.
 *
 * C11, no external deps.
 */
#ifndef WB_MANIFEST_H
#define WB_MANIFEST_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *path;         /* asset path (owned) */
    char  hash[17];     /* content hash at manifest time */
} wb_manifest_entry;

typedef struct {
    wb_manifest_entry *entries;   /* owned */
    int count;
} wb_manifest;

void wb_manifest_free(wb_manifest *mf);

/* Load a manifest (JSON: {"assets":[{"path":"...","hash":"..."}]}).
 * Returns 0 on success. */
int  wb_manifest_load(const char *path, wb_manifest *out);

/* Write a manifest for the given paths, hashing each. Returns 0 on success. */
int  wb_manifest_write(const char *path, const char **paths, int count);

/* 1 if every manifest entry still matches the file on disk. */
int  wb_manifest_verify(const wb_manifest *mf);

#ifdef __cplusplus
}
#endif

#endif /* WB_MANIFEST_H */

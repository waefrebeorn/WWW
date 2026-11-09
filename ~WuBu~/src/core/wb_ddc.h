/*
 * wb_ddc.h -- WuBu Engine content-addressed derived-data cache.
 *
 * Derived (engine-ready) models are stored under the source file's content
 * hash, so an incremental build never trusts a timestamp: if the source
 * changed, its hash changed, and the cache entry is simply absent.
 *
 * C11, no external deps.
 */
#ifndef WB_DDC_H
#define WB_DDC_H

#include "wb_assets.h"   /* wb_model */

#ifdef __cplusplus
extern "C" {
#endif

/* Point the DDC at a directory. Creates it if needed. Pass NULL to disable. */
int  wb_ddc_init(const char *cache_dir);

/* 1 if a cache entry exists for `src_path` and matches the source's current
 * content hash, 0 otherwise (and if the DDC is disabled). */
int  wb_ddc_is_fresh(const char *src_path);

/* Store a derived model under the source's content hash. Returns 0 on success. */
int  wb_ddc_save(const char *src_path, const wb_model *m);

/* Load a derived model from the cache. Returns 0 on success. */
int  wb_ddc_load(const char *src_path, wb_model *out);

#ifdef __cplusplus
}
#endif

#endif /* WB_DDC_H */

/*
 * wb_assets_internal.h -- private interface of the content pipeline module.
 *
 * Not a public header: it is included only by the wb_assets*.c translation
 * units. It carries the module's shared error buffer and the one internal
 * call (mesh decoding) that the top-level loader delegates to.
 */
#ifndef WB_ASSETS_INTERNAL_H
#define WB_ASSETS_INTERNAL_H

#include "wb_assets.h"
#include "wb_json.h"

#define WB_ASSETS_ERR_SZ 512
extern char wb_assets_g_err[WB_ASSETS_ERR_SZ];

/* Decode one glTF mesh's primitives into `m`. `bin` is the GLB/binary buffer
 * the accessors index into. */
void wb_assets_load_mesh(wb_model *m, const wb_json *root, const wb_json *mesh,
                         int mesh_index, const unsigned char *bin,
                         size_t bin_len);

#endif /* WB_ASSETS_INTERNAL_H */

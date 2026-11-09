/*
 * wb_gltf.h -- WuBu Engine shared glTF accessor decoding.
 *
 * Both the content pipeline (wb_assets) and the avatar loader (wb_avatar) read
 * the same glTF buffer/accessor structure. This is the single implementation;
 * neither module carries its own copy of the component-type table or the
 * accessor-resolution logic.
 *
 * C11, no external deps beyond wb_json.
 */
#ifndef WB_GLTF_H
#define WB_GLTF_H

#include <stddef.h>
#include "wb_json.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A resolved accessor: where its bytes live and how to walk them. */
typedef struct {
    const unsigned char *data;   /* first byte of element 0 */
    size_t              len;     /* total bytes covered by the accessor */
    int                 component_type;  /* 5120..5126 */
    int                 count;   /* element count */
    int                 type;    /* glTF type code: 1..4, 16=MAT4 */
    int                 comps;   /* scalar components per element */
    int                 byte_stride;  /* packed when the JSON omits it */
} wb_gltf_accessor;

/* Bytes per component for a glTF componentType (5120..5126), or 0. */
int wb_gltf_comp_size(int component_type);

/* Scalar components per glTF type code (SCALAR/VEC2/VEC3/VEC4/MAT4), or 0. */
int wb_gltf_type_comps(int type);

/* Read component `idx` of one element as a float. 0.0f for an unknown type. */
float wb_gltf_read_comp(const unsigned char *base, int component_type, int idx);

/* Resolve accessor `acc_index` of `root` against the binary buffer `bin`.
 * Returns the accessor's byte pointer, or NULL on failure. On success `out`
 * is filled in. */
const unsigned char *wb_gltf_accessor_bytes(const wb_json *root,
                                            const unsigned char *bin,
                                            size_t bin_len, int acc_index,
                                            wb_gltf_accessor *out);

/* Read `index`'s `comps` floats into `out` (row-major). 0 on success. */
int wb_gltf_read_floats(const wb_gltf_accessor *a, int index, int comps,
                        float *out);

/* 1 when `file` starts with a GLB magic header (regardless of version). */
int wb_gltf_is_glb(const unsigned char *file, size_t len);

/* Split a GLB 2.0 container in place: `json`/`bin` receive pointers into
 * `file` (which must outlive them) and their lengths. Returns 0 on success,
 * -1 when `file` is not a GLB or the version is unsupported. */
int wb_gltf_split_glb(const unsigned char *file, size_t len,
                      const char **json, size_t *json_len,
                      const unsigned char **bin, size_t *bin_len);

#ifdef __cplusplus
}
#endif

#endif /* WB_GLTF_H */

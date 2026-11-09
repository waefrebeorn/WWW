/*
 * wb_avatar_internal.h -- private interface of the avatar module.
 *
 * Included only by the wb_avatar*.c translation units: the module's shared
 * error buffer and the two internal glTF decoders the top-level loader calls.
 */
#ifndef WB_AVATAR_INTERNAL_H
#define WB_AVATAR_INTERNAL_H

#include "wb_anim.h"
#include "wb_avatar.h"
#include "wb_json.h"

#define WB_AVATAR_ERR_SZ 512
extern char wb_avatar_g_err[WB_AVATAR_ERR_SZ];

/* Decode the first skin into `rig` (joints + inverse bind matrices + the
 * humanoid bone mapping). */
void wb_avatar_load_rig(const wb_json *root, const unsigned char *bin,
                        size_t bin_len, wb_rig *rig);

/* Decode one glTF animation into `out`. */
void wb_avatar_load_animation(const wb_json *root, const unsigned char *bin,
                              size_t bin_len, const wb_json *anim,
                              wb_animation *out);

#endif /* WB_AVATAR_INTERNAL_H */

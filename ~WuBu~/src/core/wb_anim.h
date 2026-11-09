/*
 * wb_anim.h -- WuBu Engine animation clip + sampling.
 *
 * Split out of the avatar module: the clip format and the sampler are useful
 * without the rig/identity half, and an animation-only caller should not pull
 * the glTF loader in.
 *
 * C11, no external deps.
 */
#ifndef WB_ANIM_H
#define WB_ANIM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WB_AVATAR_MAX_CHAN   256
#define WB_AVATAR_MAX_KEYS   64

typedef struct {
    int   node;
    int   path;          /* 0 = translation, 1 = rotation (quat), 2 = scale */
    int   nkeys;
    float times[WB_AVATAR_MAX_KEYS];
    float values[WB_AVATAR_MAX_KEYS][4];
} wb_anim_channel;

/* A node's rest (bind-pose) TRS, used as the base an animation overrides. */
typedef struct {
    float t[3];
    float q[4];          /* x, y, z, w */
    float s[3];
} wb_trs;

typedef struct {
    char            name[64];
    wb_anim_channel channels[WB_AVATAR_MAX_CHAN];
    int             nchannels;
    float           duration;
    wb_trs          rest[256];   /* indexed by glTF node index */
} wb_animation;

/* Sample an animation at time `t` (seconds, clamped to the clip). Writes a
 * column-major local TRS mat4 per glTF node into `out_local` (max_nodes
 * entries). Nodes with no channel keep identity. Returns the number written. */
int wb_avatar_sample(const wb_animation *a, float t,
                     float out_local[][16], int max_nodes);

#ifdef __cplusplus
}
#endif

#endif /* WB_ANIM_H */

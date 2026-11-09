/*
 * wb_avatar.h -- WuBu Engine avatar + identity (slice 14).
 *
 * The "meta" in meta engine: an avatar that persists across experiences.
 * Imports the glTF skin (joints + inverse bind matrices), maps joints onto the
 * standard humanoid rig (the VRM / Unity Humanoid bone set, which Khronos and
 * the VRM Consortium are converging on), and stores a small identity record so
 * the same avatar walks into any world. Animation clips and the sampler live in
 * wb_anim.h; mesh loading in wb_assets.h.
 *
 * C11, no external deps.
 */
#ifndef WB_AVATAR_H
#define WB_AVATAR_H

#include "wb_assets.h"   /* wb_model */
#include "wb_anim.h"     /* wb_animation */

#ifdef __cplusplus
extern "C" {
#endif

/* The standard humanoid bone set (VRM 1.0 / Unity Humanoid subset). */
typedef enum {
    WB_BONE_HIPS = 0,
    WB_BONE_SPINE,
    WB_BONE_CHEST,
    WB_BONE_HEAD,
    WB_BONE_LEFT_UPPER_ARM,
    WB_BONE_LEFT_LOWER_ARM,
    WB_BONE_LEFT_HAND,
    WB_BONE_RIGHT_UPPER_ARM,
    WB_BONE_RIGHT_LOWER_ARM,
    WB_BONE_RIGHT_HAND,
    WB_BONE_LEFT_UPPER_LEG,
    WB_BONE_LEFT_LOWER_LEG,
    WB_BONE_LEFT_FOOT,
    WB_BONE_RIGHT_UPPER_LEG,
    WB_BONE_RIGHT_LOWER_LEG,
    WB_BONE_RIGHT_FOOT,
    WB_BONE_COUNT
} wb_bone;

#define WB_AVATAR_MAX_JOINTS 128

typedef struct {
    int  node;          /* glTF node index, -1 when unmapped */
    int  parent;        /* wb_bone index, or -1 for the root */
    char name[64];
} wb_rig_bone;

typedef struct {
    wb_rig_bone bones[WB_BONE_COUNT];
    int         nmapped;
    int         njoints;
    int         joint_nodes[WB_AVATAR_MAX_JOINTS];
    float       inv_bind[WB_AVATAR_MAX_JOINTS][16];  /* column-major mat4 */
} wb_rig;

/* A persistent identity: which avatar, which rig, how tall. */
typedef struct {
    char  avatar_id[64];
    char  model_path[256];
    char  rig_name[64];
    float height;        /* metres */
} wb_avatar;

const char *wb_avatar_error(void);

/* Load a glTF/GLB avatar: meshes into `out_model`, the skin into `out_rig`,
 * and up to `max_anims` animations. Returns 0 on success. */
int wb_avatar_load(const char *path, wb_model *out_model, wb_rig *out_rig,
                   wb_animation *out_anims, int max_anims, int *out_nanims);

/* Identity persistence (JSON). */
int wb_avatar_identity_save(const char *path, const wb_avatar *a);
int wb_avatar_identity_load(const char *path, wb_avatar *a);

/* Bone name helpers (exposed for tools/debug). */
const char *wb_bone_name(int bone);
int         wb_bone_from_name(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* WB_AVATAR_H */

/*
 * wb_avatar_bones.c -- the humanoid bone name table and its lookup.
 * See wb_avatar.h.
 */
#include "wb_avatar.h"

#include <string.h>

static const char *kBoneNames[WB_BONE_COUNT] = {
    "hips", "spine", "chest", "head",
    "left_upper_arm", "left_lower_arm", "left_hand",
    "right_upper_arm", "right_lower_arm", "right_hand",
    "left_upper_leg", "left_lower_leg", "left_foot",
    "right_upper_leg", "right_lower_leg", "right_foot"
};

const char *wb_bone_name(int bone) {
    return (bone >= 0 && bone < WB_BONE_COUNT) ? kBoneNames[bone] : "?";
}

static void lower_copy(const char *src, char *dst, size_t dst_sz) {
    size_t i = 0;
    for (; src[i] && i + 1 < dst_sz; i++) {
        char c = src[i];
        dst[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    dst[i] = 0;
}

int wb_bone_from_name(const char *name) {
    if (!name) return -1;
    char low[128];
    lower_copy(name, low, sizeof low);

    /* Longest / most specific first so "left_upper_arm" is not eaten by
     * a bare "arm" test. */
    struct { const char *needle; int bone; } table[] = {
        /* Mixamo / VRM style names have no underscores, so match both forms. */
        { "leftupperarm", WB_BONE_LEFT_UPPER_ARM },
        { "leftuparm",    WB_BONE_LEFT_UPPER_ARM },
        { "leftarm",      WB_BONE_LEFT_UPPER_ARM },
        { "leftforearm",  WB_BONE_LEFT_LOWER_ARM },
        { "lefthand",     WB_BONE_LEFT_HAND },
        { "rightupperarm", WB_BONE_RIGHT_UPPER_ARM },
        { "rightuparm",   WB_BONE_RIGHT_UPPER_ARM },
        { "rightarm",     WB_BONE_RIGHT_UPPER_ARM },
        { "rightforearm", WB_BONE_RIGHT_LOWER_ARM },
        { "righthand",    WB_BONE_RIGHT_HAND },
        { "leftupperleg", WB_BONE_LEFT_UPPER_LEG },
        { "leftupleg",    WB_BONE_LEFT_UPPER_LEG },
        { "leftleg",      WB_BONE_LEFT_UPPER_LEG },
        { "leftfoot",     WB_BONE_LEFT_FOOT },
        { "rightupperleg", WB_BONE_RIGHT_UPPER_LEG },
        { "rightupleg",   WB_BONE_RIGHT_UPPER_LEG },
        { "rightleg",     WB_BONE_RIGHT_UPPER_LEG },
        { "rightfoot",    WB_BONE_RIGHT_FOOT },
        { "hips",            WB_BONE_HIPS },
        { "pelvis",          WB_BONE_HIPS },
        { "spine",           WB_BONE_SPINE },
        { "chest",           WB_BONE_CHEST },
        { "neck",            WB_BONE_HEAD },
        { "head",            WB_BONE_HEAD },
    };
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++)
        if (strstr(low, table[i].needle)) return table[i].bone;
    return -1;
}

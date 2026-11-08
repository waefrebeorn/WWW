/*
 * wb_avatar_test.c -- slice 14 gate: avatar + identity.
 *
 * Builds a GLB in memory with a two-joint humanoid skin (Hips -> Spine),
 * inverse bind matrices, and a 1-second rotation animation on the Spine, then
 * verifies the rig mapping, animation sampling and identity persistence.
 */
#include "wb_avatar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static int nearly(float a, float b, float tol) { return fabsf(a - b) <= tol; }

static void put_u32(unsigned char **p, unsigned v) {
    (*p)[0] = (unsigned char)(v & 0xff);
    (*p)[1] = (unsigned char)((v >> 8) & 0xff);
    (*p)[2] = (unsigned char)((v >> 16) & 0xff);
    (*p)[3] = (unsigned char)((v >> 24) & 0xff);
    *p += 4;
}

static const char *kAvatarJSON =
    "{"
    "\"asset\":{\"version\":\"2.0\"},"
    "\"scenes\":[{\"nodes\":[0]}],"
    "\"nodes\":["
    "{\"name\":\"Hips\",\"children\":[1],\"translation\":[0,1,0]},"
    "{\"name\":\"Spine\",\"translation\":[0,0.3,0]}"
    "],"
    "\"meshes\":[{\"primitives\":[{"
    "\"attributes\":{\"POSITION\":0,\"NORMAL\":1},\"indices\":2}]}],"
    "\"skins\":[{\"joints\":[0,1],\"inverseBindMatrices\":3}],"
    "\"animations\":[{"
    "\"name\":\"idle\","
    "\"samplers\":[{\"input\":4,\"output\":5}],"
    "\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"rotation\"}}]"
    "}],"
    "\"accessors\":["
    "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
    "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
    "{\"bufferView\":2,\"componentType\":5125,\"count\":3,\"type\":\"SCALAR\"},"
    "{\"bufferView\":3,\"componentType\":5126,\"count\":2,\"type\":\"MAT4\"},"
    "{\"bufferView\":4,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\"},"
    "{\"bufferView\":5,\"componentType\":5126,\"count\":2,\"type\":\"VEC4\"}"
    "],"
    "\"bufferViews\":["
    "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
    "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":36},"
    "{\"buffer\":0,\"byteOffset\":72,\"byteLength\":12},"
    "{\"buffer\":0,\"byteOffset\":84,\"byteLength\":128},"
    "{\"buffer\":0,\"byteOffset\":212,\"byteLength\":8},"
    "{\"buffer\":0,\"byteOffset\":220,\"byteLength\":32}"
    "],"
    "\"buffers\":[{\"byteLength\":252}]"
    "}";

static size_t build_avatar_glb(const char *path) {
    static const float pos[9]  = { -0.5f,-0.5f,0.0f,  0.5f,-0.5f,0.0f,  0.0f,0.5f,0.0f };
    static const float nrm[9]  = { 0,0,1, 0,0,1, 0,0,1 };
    static const unsigned idx[3] = { 0, 1, 2 };

    /* two identity inverse bind matrices */
    float ibm[32];
    for (int m = 0; m < 2; m++) {
        float *M = ibm + m * 16;
        memset(M, 0, 64);
        M[0] = M[5] = M[10] = M[15] = 1.0f;
    }

    static const float times[2] = { 0.0f, 1.0f };
    /* 0 deg then 45 deg about z (quat x,y,z,w) */
    static const float quats[8] = { 0,0,0,1,  0,0,0.3826834f,0.9238795f };

    size_t json_len = strlen(kAvatarJSON);
    size_t json_pad = (4 - (json_len % 4)) % 4;
    size_t bin_len = 252;
    size_t bin_pad = (4 - (bin_len % 4)) % 4;
    size_t total = 12 + 8 + json_len + json_pad + 8 + bin_len + bin_pad;

    unsigned char *buf = (unsigned char *)calloc(1, total);
    unsigned char *p = buf;
    memcpy(p, "glTF", 4); p += 4;
    put_u32(&p, 2);
    put_u32(&p, (unsigned)total);

    put_u32(&p, (unsigned)(json_len + json_pad));
    put_u32(&p, 0x4E4F534A);
    memcpy(p, kAvatarJSON, json_len);
    for (size_t i = 0; i < json_pad; i++) p[json_len + i] = ' ';
    p += json_len + json_pad;

    put_u32(&p, (unsigned)(bin_len + bin_pad));
    put_u32(&p, 0x004E4942);
    memcpy(p, pos, sizeof pos); p += sizeof pos;
    memcpy(p, nrm, sizeof nrm); p += sizeof nrm;
    memcpy(p, idx, sizeof idx); p += sizeof idx;
    memcpy(p, ibm, sizeof ibm); p += sizeof ibm;
    memcpy(p, times, sizeof times); p += sizeof times;
    memcpy(p, quats, sizeof quats); p += sizeof quats;
    p += bin_pad;

    FILE *f = fopen(path, "wb");
    if (!f) { free(buf); return 0; }
    size_t wrote = fwrite(buf, 1, total, f);
    fclose(f);
    free(buf);
    return wrote;
}

int main(void) {
    const char *glb = "build/test_avatar.glb";
    CHECK(build_avatar_glb(glb) > 0, "avatar GLB written");

    /* ---- 1: bone name mapping --------------------------------------- */
    CHECK(wb_bone_from_name("Hips") == WB_BONE_HIPS, "'Hips' maps to the hips bone");
    CHECK(wb_bone_from_name("Spine") == WB_BONE_SPINE, "'Spine' maps to the spine bone");
    CHECK(wb_bone_from_name("Head") == WB_BONE_HEAD, "'Head' maps to the head bone");
    CHECK(wb_bone_from_name("LeftArm") == WB_BONE_LEFT_UPPER_ARM,
          "'LeftArm' maps to the left upper arm");
    CHECK(wb_bone_from_name("mixamorig:RightForeArm") == WB_BONE_RIGHT_LOWER_ARM,
          "a Mixamo-style name maps to the right lower arm");
    CHECK(wb_bone_from_name("some_random_mesh") == -1, "an unknown name does not map");

    /* ---- 2: rig import ---------------------------------------------- */
    wb_model model;
    wb_rig rig;
    wb_animation anims[4];
    int nanims = 0;
    memset(&model, 0, sizeof model);
    memset(&rig, 0, sizeof rig);

    CHECK(wb_avatar_load(glb, &model, &rig, anims, 4, &nanims) == 0, "avatar loaded");
    CHECK(wb_avatar_error() == NULL, "no avatar load error");

    CHECK(rig.njoints == 2, "two joints imported");
    CHECK(rig.nmapped == 2, "both joints mapped onto the humanoid rig");
    CHECK(rig.bones[WB_BONE_HIPS].node == 0, "Hips maps to node 0");
    CHECK(rig.bones[WB_BONE_SPINE].node == 1, "Spine maps to node 1");
    CHECK(rig.bones[WB_BONE_SPINE].parent == WB_BONE_HIPS,
          "Spine's parent is Hips (from the glTF node hierarchy)");
    CHECK(nearly(rig.inv_bind[0][0], 1.0f, 1e-6) &&
          nearly(rig.inv_bind[1][15], 1.0f, 1e-6),
          "inverse bind matrices imported");

    CHECK(nanims == 1, "one animation imported");
    if (nanims == 1) {
        CHECK(!strcmp(anims[0].name, "idle"), "animation name imported");
        CHECK(anims[0].nchannels == 1, "animation has one channel");
        CHECK(anims[0].duration > 0.99f && anims[0].duration < 1.01f,
              "animation duration imported");
        CHECK(anims[0].channels[0].node == 1, "channel targets the Spine node");
        CHECK(anims[0].channels[0].path == 1, "channel drives rotation");
    }

    /* ---- 3: animation sampling -------------------------------------- */
    if (nanims == 1) {
        float local[8][16];
        int n0 = wb_avatar_sample(&anims[0], 0.0f, local, 8);
        CHECK(n0 == 1, "sampling wrote one node transform");
        /* at t=0 the rotation is identity, so the matrix is a pure translation */
        CHECK(nearly(local[1][12], 0.0f, 1e-5) &&
              nearly(local[1][13], 0.3f, 1e-5),
              "t=0 gives the rest translation");

        float local2[8][16];
        wb_avatar_sample(&anims[0], 1.0f, local2, 8);
        /* at t=1 the spine is rotated 45 deg about z: the basis vectors change */
        CHECK(!nearly(local2[1][0], local[1][0], 1e-4) ||
              !nearly(local2[1][1], local[1][1], 1e-4),
              "t=1 differs from t=0 (the joint actually rotates)");
        /* 45 deg about z: cos = 0.7071 */
        CHECK(nearly(local2[1][0], 0.7071f, 0.01f) &&
              nearly(local2[1][1], 0.7071f, 0.01f),
              "t=1 rotation is 45 degrees about z");

        /* out-of-range times clamp instead of exploding */
        float local3[8][16];
        wb_avatar_sample(&anims[0], 99.0f, local3, 8);
        CHECK(nearly(local3[1][0], local2[1][0], 1e-4), "time beyond the clip clamps");
    }

    /* ---- 4: identity persistence ------------------------------------ */
    wb_avatar a;
    memset(&a, 0, sizeof a);
    snprintf(a.avatar_id, sizeof a.avatar_id, "waefre-001");
    snprintf(a.model_path, sizeof a.model_path, "%s", glb);
    snprintf(a.rig_name, sizeof a.rig_name, "humanoid");
    a.height = 1.80f;

    CHECK(wb_avatar_identity_save("build/test_avatar.json", &a) == 0,
          "identity saved");
    wb_avatar b;
    CHECK(wb_avatar_identity_load("build/test_avatar.json", &b) == 0,
          "identity loaded");
    CHECK(!strcmp(b.avatar_id, "waefre-001"), "identity id round-trips");
    CHECK(!strcmp(b.model_path, glb), "identity model path round-trips");
    CHECK(!strcmp(b.rig_name, "humanoid"), "identity rig round-trips");
    CHECK(nearly(b.height, 1.80f, 1e-4), "identity height round-trips");

    wb_model_free(&model);
    printf("\n=== AVATAR TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

/*
 * wb_assets_test.c -- slice 12 gate: the content pipeline.
 *
 * Builds a real GLB in memory (JSON chunk + BIN chunk), writes it to disk, and
 * exercises the whole pipeline:
 *   glTF/GLB parse -> mesh with the right geometry and material
 *   content-addressed DDC -> save, freshness, load, round-trip equality
 *   manifest -> write, load, verify (and detect a modified asset)
 */
#include "wb_assets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static int nearly(float a, float b, float tol) { return fabsf(a - b) <= tol; }

/* ------------------------------------------------------------------ GLB */
/* Minimal glTF 2.0 binary container: a triangle with normals and a material. */

static void put_u32(unsigned char **p, unsigned v) {
    (*p)[0] = (unsigned char)(v & 0xff);
    (*p)[1] = (unsigned char)((v >> 8) & 0xff);
    (*p)[2] = (unsigned char)((v >> 16) & 0xff);
    (*p)[3] = (unsigned char)((v >> 24) & 0xff);
    *p += 4;
}

static const char *kGltfJSON =
    "{"
    "\"asset\":{\"version\":\"2.0\"},"
    "\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
    "\"meshes\":[{\"primitives\":[{"
    "\"attributes\":{\"POSITION\":0,\"NORMAL\":1},\"indices\":2,\"material\":0}]}],"
    "\"materials\":[{\"pbrMetallicRoughness\":{"
    "\"baseColorFactor\":[0.9,0.2,0.3,1.0],\"metallicFactor\":0.0,"
    "\"roughnessFactor\":0.5}}],"
    "\"accessors\":["
    "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
    "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
    "{\"bufferView\":2,\"componentType\":5125,\"count\":3,\"type\":\"SCALAR\"}],"
    "\"bufferViews\":["
    "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36,\"target\":34962},"
    "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":36,\"target\":34962},"
    "{\"buffer\":0,\"byteOffset\":72,\"byteLength\":12,\"target\":34963}],"
    "\"buffers\":[{\"byteLength\":84}]"
    "}";

static size_t build_glb(const char *path) {
    static const float pos[9] = { -0.5f, -0.5f, 0.0f,   0.5f, -0.5f, 0.0f,   0.0f, 0.5f, 0.0f };
    static const float nrm[9] = { 0,0,1,  0,0,1,  0,0,1 };
    static const unsigned idx[3] = { 0, 1, 2 };

    size_t json_len = strlen(kGltfJSON);
    size_t json_pad = (4 - (json_len % 4)) % 4;
    size_t bin_len = sizeof pos + sizeof nrm + sizeof idx;
    size_t bin_pad = (4 - (bin_len % 4)) % 4;
    size_t total = 12 + 8 + json_len + json_pad + 8 + bin_len + bin_pad;

    unsigned char *buf = (unsigned char *)calloc(1, total);
    unsigned char *p = buf;
    memcpy(p, "glTF", 4); p += 4;
    put_u32(&p, 2);
    put_u32(&p, (unsigned)total);

    /* JSON chunk */
    put_u32(&p, (unsigned)(json_len + json_pad));
    put_u32(&p, 0x4E4F534A);
    memcpy(p, kGltfJSON, json_len);
    for (size_t i = 0; i < json_pad; i++) p[json_len + i] = ' ';
    p += json_len + json_pad;

    /* BIN chunk */
    put_u32(&p, (unsigned)(bin_len + bin_pad));
    put_u32(&p, 0x004E4942);
    memcpy(p, pos, sizeof pos); p += sizeof pos;
    memcpy(p, nrm, sizeof nrm); p += sizeof nrm;
    memcpy(p, idx, sizeof idx);  p += sizeof idx;
    p += bin_pad;

    FILE *f = fopen(path, "wb");
    if (!f) { free(buf); return 0; }
    size_t wrote = fwrite(buf, 1, total, f);
    fclose(f);
    free(buf);
    return wrote;
}

/* ------------------------------------------------------------------ tests */

int main(void) {
    const char *glb = "build/test_triangle.glb";

    size_t wrote = build_glb(glb);
    CHECK(wrote > 0, "GLB written to disk");

    /* ---- 1: parse ---------------------------------------------------- */
    wb_model m;
    CHECK(wb_asset_load_gltf(glb, &m) == 0, "GLB loaded");
    CHECK(wb_asset_error() == NULL, "no parse error");
    CHECK(m.nmeshes == 1, "one mesh loaded");
    if (m.nmeshes >= 1) {
        wb_mesh *mm = &m.meshes[0];
        CHECK(mm->nverts == 3, "three vertices");
        CHECK(mm->nindices == 3, "three indices");
        CHECK(nearly(mm->verts[0].pos[0], -0.5f, 1e-5) &&
              nearly(mm->verts[2].pos[1],  0.5f, 1e-5),
              "vertex positions match the source data");
        CHECK(nearly(mm->verts[0].normal[2], 1.0f, 1e-5), "normals loaded");
        CHECK(mm->indices[0] == 0 && mm->indices[2] == 2, "indices loaded");
        CHECK(nearly(mm->base_color[0], 0.9f, 1e-5) &&
              nearly(mm->base_color[1], 0.2f, 1e-5) &&
              nearly(mm->base_color[2], 0.3f, 1e-5),
              "base color factor loaded");
        CHECK(nearly(mm->metallic, 0.0f, 1e-5), "metallic factor loaded");
        CHECK(nearly(mm->roughness, 0.5f, 1e-5), "roughness factor loaded");
    }

    /* ---- 2: content hash -------------------------------------------- */
    char h1[17], h2[17];
    CHECK(wb_content_hash(glb, h1) == 0 && strlen(h1) == 16, "content hash computed");
    CHECK(wb_content_hash(glb, h2) == 0 && strcmp(h1, h2) == 0,
          "content hash is stable across calls");

    /* ---- 3: DDC round trip ------------------------------------------ */
    system("rmdir /s /q build\\ddc 2>nul");   /* start from a cold cache */
    CHECK(wb_ddc_init("build/ddc") == 0, "DDC initialised");
    CHECK(!wb_ddc_is_fresh(glb), "DDC is cold before the first save");
    CHECK(wb_ddc_save(glb, &m) == 0, "derived model saved to the DDC");
    CHECK(wb_ddc_is_fresh(glb), "DDC is fresh after saving");

    wb_model m2;
    CHECK(wb_ddc_load(glb, &m2) == 0, "derived model loaded from the DDC");
    CHECK(m2.nmeshes == m.nmeshes, "DDC round trip preserves mesh count");
    if (m2.nmeshes == m.nmeshes && m.nmeshes > 0) {
        CHECK(m2.meshes[0].nverts == m.meshes[0].nverts &&
              m2.meshes[0].nindices == m.meshes[0].nindices,
              "DDC round trip preserves geometry counts");
        CHECK(nearly(m2.meshes[0].verts[1].pos[0], m.meshes[0].verts[1].pos[0], 1e-6),
              "DDC round trip preserves vertex data");
        CHECK(nearly(m2.meshes[0].base_color[0], 0.9f, 1e-6),
              "DDC round trip preserves material data");
    }
    wb_model_free(&m2);

    /* ---- 4: manifest ------------------------------------------------ */
    const char *paths[1] = { glb };
    CHECK(wb_manifest_write("build/test_manifest.json", paths, 1) == 0,
          "manifest written");
    wb_manifest mf;
    CHECK(wb_manifest_load("build/test_manifest.json", &mf) == 0, "manifest loaded");
    CHECK(mf.count == 1, "manifest has one entry");
    CHECK(wb_manifest_verify(&mf) == 1, "manifest verifies against the file");

    /* touch the asset: the manifest must notice */
    {
        FILE *f = fopen(glb, "ab");
        if (f) { fputc(0, f); fclose(f); }
    }
    CHECK(wb_manifest_verify(&mf) == 0,
          "manifest detects a modified asset (content-addressed)");
    wb_manifest_free(&mf);

    /* ---- 5: error handling ------------------------------------------ */
    wb_model bad;
    CHECK(wb_asset_load_gltf("build/does_not_exist.glb", &bad) == -1,
          "missing file is rejected");
    CHECK(wb_asset_error() != NULL, "the load error is reported");

    wb_model_free(&m);
    printf("\n=== ASSET TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

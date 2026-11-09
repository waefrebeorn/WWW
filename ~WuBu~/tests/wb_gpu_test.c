/*
 * wb_gpu_test.c -- slice 11 gate: the D3D12 GPU backend.
 *
 * Renders for real and reads pixels back, so a green gate means the GPU path
 * actually works -- not just that the API calls returned success:
 *   1. a clear pass produces the exact clear colour
 *   2. an HLSL-shaded triangle covers the centre of the target
 *   3. a texture written by one pass can be read as a shader resource by the
 *      next (i.e. the barrier derivation is correct)
 *   4. a bad shader is reported, not silently ignored
 *
 * Uses WARP (the software adapter) by default so the gate runs anywhere;
 * set use_warp=0 to exercise the real GPU.
 */
#include "wb_gpu.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static int nearly(float a, float b, float tol) { return fabsf(a - b) <= tol; }

/* A flat-coloured triangle: no constant buffer, no textures. */
static const char *kTriVS =
    "struct VSIn { float3 pos : POSITION; float3 normal : NORMAL; };\n"
    "struct VSOut { float4 pos : SV_POSITION; float3 normal : NORMAL; };\n"
    "VSOut vs_main(VSIn i) {\n"
    "  VSOut o; o.pos = float4(i.pos, 1.0); o.normal = i.normal; return o;\n"
    "}\n";

static const char *kTriPS =
    "struct VSOut { float4 pos : SV_POSITION; float3 normal : NORMAL; };\n"
    "float4 ps_main(VSOut i) : SV_TARGET {\n"
    "  return float4(0.20, 0.70, 0.40, 1.0);\n"
    "}\n";

/* A textured pass: samples t0, proving a texture written by an earlier pass
 * can be bound as a shader resource. */
static const char *kTexVS =
    "struct VSIn { float3 pos : POSITION; float3 normal : NORMAL; };\n"
    "struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };\n"
    "VSOut vs_main(VSIn i) {\n"
    "  VSOut o; o.pos = float4(i.pos, 1.0);\n"
    "  o.uv = float2(i.pos.x * 0.5 + 0.5, i.pos.y * 0.5 + 0.5); return o;\n"
    "}\n";

static const char *kTexPS =
    "Texture2D tex : register(t0);\n"
    "SamplerState samp : register(s0);\n"
    "struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };\n"
    "float4 ps_main(VSOut i) : SV_TARGET {\n"
    "  return tex.Sample(samp, i.uv);\n"
    "}\n";

typedef struct { float pos[3]; float normal[3]; } wb_test_vertex;

/* Wound clockwise in screen space (D3D's front face with FrontCounterClockwise
 * = FALSE), so back-face culling keeps it. */
static wb_test_vertex kTri[3] = {
    { { -0.6f, -0.6f, 0.5f }, { 0, 0, 1 } },
    { {  0.0f,  0.6f, 0.5f }, { 0, 0, 1 } },
    { {  0.6f, -0.6f, 0.5f }, { 0, 0, 1 } },
};

int main(void) {
    wb_gpu_desc desc;
    desc.width = 0; desc.height = 0; desc.use_warp = 1;

    wb_gpu *g = wb_gpu_create(&desc);
    if (!g) { printf("SKIP: no D3D12 device (%s)\n", wb_gpu_error(g)); return 0; }

    CHECK(strcmp(wb_gpu_backend_name(g), "d3d12") == 0, "backend is d3d12");
    CHECK(wb_gpu_ok(g), "device created without error");

    char dev[128]; wb_gpu_get_device_name(g, dev, sizeof dev);
    printf("  gpu: %s\n", dev);

    /* ---- 1: a clear pass produces the exact clear colour --------------- */
    wb_gpu_texture *rt = wb_gpu_texture_create(g, WB_GPU_FMT_RGBA8, 64, 64, NULL);
    CHECK(rt != NULL, "render target created");
    CHECK(wb_gpu_texture_width(rt) == 64 && wb_gpu_texture_height(rt) == 64,
          "render target dimensions");

    float clear_col[4] = { 0.10f, 0.20f, 0.30f, 1.0f };
    wb_gpu_pass *p = wb_gpu_pass_begin(g, "clear");
    wb_gpu_pass_set_color(p, 0, rt, clear_col);
    wb_gpu_pass_end(p);
    CHECK(wb_gpu_execute(g) == 0, "clear graph executed");

    float px[4];
    CHECK(wb_gpu_read_pixel(g, rt, px) == 0, "readback of the clear");
    CHECK(nearly(px[0], clear_col[0], 0.02f) && nearly(px[1], clear_col[1], 0.02f) &&
          nearly(px[2], clear_col[2], 0.02f),
          "cleared pixel matches the requested colour");

    /* ---- 2: an HLSL-shaded triangle covers the centre ------------------ */
    wb_gpu_shader *vs = wb_gpu_shader_create(g, WB_GPU_SHADER_VERTEX, kTriVS, "vs_main");
    wb_gpu_shader *ps = wb_gpu_shader_create(g, WB_GPU_SHADER_PIXEL, kTriPS, "ps_main");
    CHECK(vs != NULL && ps != NULL, "shaders compiled from HLSL at runtime");

    wb_gpu_pipeline *pipe = wb_gpu_pipeline_create(g, vs, ps);
    CHECK(pipe != NULL, "pipeline created");

    wb_gpu_buffer *vb = wb_gpu_buffer_create(g, WB_GPU_BUF_VERTEX,
                                            sizeof kTri, kTri);
    CHECK(vb != NULL, "vertex buffer created");
    CHECK(wb_gpu_buffer_size(vb) == sizeof kTri, "vertex buffer size");

    p = wb_gpu_pass_begin(g, "triangle");
    wb_gpu_pass_set_color(p, 0, rt, clear_col);
    wb_gpu_pass_draw(p, pipe, vb, NULL, 3, 0, 1, 0);
    wb_gpu_pass_end(p);
    CHECK(wb_gpu_execute(g) == 0, "triangle graph executed");

    CHECK(wb_gpu_read_pixel_at(g, rt, 32, 32, px) == 0, "readback of the centre pixel");
    CHECK(nearly(px[0], 0.20f, 0.05f) && nearly(px[1], 0.70f, 0.05f) &&
          nearly(px[2], 0.40f, 0.05f),
          "centre pixel is the shaded triangle colour");

    /* a corner must still be the clear colour (so the draw was real) */
    CHECK(wb_gpu_read_pixel_at(g, rt, 2, 2, px) == 0, "readback of a corner pixel");
    CHECK(nearly(px[0], clear_col[0], 0.02f) && nearly(px[1], clear_col[1], 0.02f),
          "corner pixel is still the clear colour");

    /* ---- 3: a texture written then read as a shader resource ----------- */
    /* Two textures: paint `paint`, then sample it while rendering to `rt`.
     * Using one texture for both would be a feedback loop, not a barrier. */
    wb_gpu_texture *paint = wb_gpu_texture_create(g, WB_GPU_FMT_RGBA8, 64, 64, NULL);
    CHECK(paint != NULL, "second render target created");

    p = wb_gpu_pass_begin(g, "paint");
    wb_gpu_pass_set_color(p, 0, paint, (const float[4]){ 0.90f, 0.10f, 0.10f, 1.0f });
    wb_gpu_pass_end(p);
    CHECK(wb_gpu_execute(g) == 0, "paint pass executed");

    wb_gpu_shader *tvs = wb_gpu_shader_create(g, WB_GPU_SHADER_VERTEX, kTexVS, "vs_main");
    wb_gpu_shader *tps = wb_gpu_shader_create(g, WB_GPU_SHADER_PIXEL, kTexPS, "ps_main");
    wb_gpu_pipeline *tpipe = wb_gpu_pipeline_create(g, tvs, tps);
    CHECK(tvs && tps && tpipe, "texture pipeline created");

    p = wb_gpu_pass_begin(g, "sample");
    wb_gpu_pass_set_color(p, 0, rt, (const float[4]){ 0.0f, 0.0f, 0.0f, 1.0f });
    wb_gpu_pass_read(p, paint, 0);
    wb_gpu_pass_draw(p, tpipe, vb, NULL, 3, 0, 1, 0);
    wb_gpu_pass_end(p);
    CHECK(wb_gpu_execute(g) == 0, "texture-sample pass executed (barrier inserted)");

    CHECK(wb_gpu_read_pixel_at(g, rt, 32, 32, px) == 0, "readback after sampling");
    CHECK(nearly(px[0], 0.90f, 0.05f) && nearly(px[1], 0.10f, 0.05f),
          "sampled texture returned the painted colour (barrier worked)");

    wb_gpu_texture_destroy(paint);

    /* ---- 4: errors are reported, not swallowed ------------------------ */
    wb_gpu_shader *bad = wb_gpu_shader_create(g, WB_GPU_SHADER_PIXEL,
                                              "this is not hlsl", "nope");
    CHECK(bad == NULL, "an invalid shader is rejected");
    CHECK(wb_gpu_error(g) != NULL, "the shader error is recorded");

    CHECK(wb_gpu_read_pixel_at(g, rt, -1, 0, px) == -1, "out-of-range read rejected");
    CHECK(wb_gpu_read_pixel_at(g, rt, 64, 0, px) == -1, "read past the edge rejected");

    wb_gpu_buffer_destroy(vb);
    wb_gpu_pipeline_destroy(pipe);
    wb_gpu_pipeline_destroy(tpipe);
    wb_gpu_shader_destroy(vs); wb_gpu_shader_destroy(ps);
    wb_gpu_shader_destroy(tvs); wb_gpu_shader_destroy(tps);
    wb_gpu_texture_destroy(rt);
    wb_gpu_destroy(g);

    printf("\n=== GPU TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

/*
 * wb_gpu.h -- WuBu Engine GPU backend (slice 11).
 *
 * A Vulkan-style RHI (device, buffers, textures, pipelines, command lists)
 * with an integrated render graph: passes declare the resources they read and
 * write, and the backend inserts the barriers between them. The software
 * rasterizer (wb_render.c) stays the null/headless backend and the test
 * oracle; this is the real path.
 *
 * WHY: the software rasterizer was the portable floor. A meta engine needs the
 * GPU -- this is the backend that makes the 1:1-Earth tile world renderable.
 *
 * Architecture follows the render-dependency-graph design used by Redot's
 * RenderingDeviceGraph (vendored as a reference at thirdparty/redot): resource
 * trackers, usage bits per pass, barrier groups derived from usage
 * transitions.
 *
 * Backend: Direct3D 12 (headers + import libs ship with MinGW; no external SDK
 * needed). HLSL is compiled at runtime with D3DCompile from the
 * d3dcompiler_47.dll that ships with Windows -- no shader toolchain required.
 *
 * C11. No external deps beyond the Windows SDK surface in MinGW.
 */
#ifndef WB_GPU_H
#define WB_GPU_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wb_gpu   wb_gpu;
typedef struct wb_gpu_buffer  wb_gpu_buffer;
typedef struct wb_gpu_texture wb_gpu_texture;
typedef struct wb_gpu_shader  wb_gpu_shader;
typedef struct wb_gpu_pipeline wb_gpu_pipeline;

/* Buffer kinds. */
typedef enum {
    WB_GPU_BUF_VERTEX = 0,
    WB_GPU_BUF_INDEX,
    WB_GPU_BUF_UNIFORM,
    WB_GPU_BUF_READBACK
} wb_gpu_buffer_kind;

/* Texture formats (the subset the engine needs). */
typedef enum {
    WB_GPU_FMT_R8 = 0,
    WB_GPU_FMT_RGBA8,
    WB_GPU_FMT_RGBA32F,
    WB_GPU_FMT_D24S8
} wb_gpu_format;

typedef enum {
    WB_GPU_SHADER_VERTEX = 0,
    WB_GPU_SHADER_PIXEL
} wb_gpu_shader_stage;

typedef struct {
    int width;      /* 0 = default 1280 */
    int height;     /* 0 = default 720  */
    int use_warp;   /* 1 = software adapter (CI / headless) */
} wb_gpu_desc;

/* Device ------------------------------------------------------------- */

wb_gpu *wb_gpu_create(const wb_gpu_desc *desc);
void    wb_gpu_destroy(wb_gpu *g);

const char *wb_gpu_error(const wb_gpu *g);
const char *wb_gpu_backend_name(const wb_gpu *g);   /* "d3d12" */
void    wb_gpu_get_device_name(const wb_gpu *g, char *out, size_t out_sz);

/* Buffers ------------------------------------------------------------ */

wb_gpu_buffer *wb_gpu_buffer_create(wb_gpu *g, wb_gpu_buffer_kind kind,
                                    size_t size, const void *initial);
void           wb_gpu_buffer_destroy(wb_gpu_buffer *b);
size_t         wb_gpu_buffer_size(const wb_gpu_buffer *b);
void          *wb_gpu_buffer_map(wb_gpu_buffer *b);
void           wb_gpu_buffer_unmap(wb_gpu_buffer *b);

/* Textures ----------------------------------------------------------- */

wb_gpu_texture *wb_gpu_texture_create(wb_gpu *g, wb_gpu_format fmt,
                                      int width, int height, const void *initial);
void           wb_gpu_texture_destroy(wb_gpu_texture *t);
int            wb_gpu_texture_width(const wb_gpu_texture *t);
int            wb_gpu_texture_height(const wb_gpu_texture *t);
wb_gpu_format  wb_gpu_texture_format(const wb_gpu_texture *t);

/* Shaders (HLSL compiled at runtime) --------------------------------- */

wb_gpu_shader *wb_gpu_shader_create(wb_gpu *g, wb_gpu_shader_stage stage,
                                    const char *hlsl, const char *entry);
void           wb_gpu_shader_destroy(wb_gpu_shader *s);

/* Pipeline ----------------------------------------------------------- */

wb_gpu_pipeline *wb_gpu_pipeline_create(wb_gpu *g, wb_gpu_shader *vs,
                                        wb_gpu_shader *ps);
void             wb_gpu_pipeline_destroy(wb_gpu_pipeline *p);

/* Render graph ------------------------------------------------------- */

typedef struct wb_gpu_pass wb_gpu_pass;

/* Begin a pass. Passes execute in the order they are begun. */
wb_gpu_pass *wb_gpu_pass_begin(wb_gpu *g, const char *name);

/* Declare a colour target. `clear_rgba` may be NULL to preserve contents.
 * Declaring a write to a texture that a previous pass read inserts a barrier. */
void wb_gpu_pass_set_color(wb_gpu_pass *pass, int slot, wb_gpu_texture *tex,
                           const float clear_rgba[4]);
void wb_gpu_pass_set_depth(wb_gpu_pass *pass, wb_gpu_texture *tex,
                           int clear, float depth, uint8_t stencil);

/* Declare a texture as read by this pass (shader input). */
void wb_gpu_pass_read(wb_gpu_pass *pass, wb_gpu_texture *tex, int slot);

/* Draw a mesh. `vb` may be NULL (no vertex buffer), `ib` may be NULL (draw
 * arrays). `instance_count` 1 = plain draw. */
void wb_gpu_pass_draw(wb_gpu_pass *pass, wb_gpu_pipeline *pipe,
                      wb_gpu_buffer *vb, wb_gpu_buffer *ib,
                      int vertex_count, int index_count,
                      int instance_count, int first_vertex);

/* Finish the pass and record it into the graph. */
void wb_gpu_pass_end(wb_gpu_pass *pass);

/* Execute the recorded graph (barriers + command submission + sync).
 * Returns 0 on success, -1 on failure (see wb_gpu_error). */
int  wb_gpu_execute(wb_gpu *g);

/* Execute and present a swapchain texture to a window (windowed builds). */
int  wb_gpu_execute_present(wb_gpu *g, void *hwnd);

/* Readback ----------------------------------------------------------- */

/* Synchronously copy a texture's pixel to out_rgba. Useful for tests.
 * Blocks until the GPU is idle. Returns 0 on success. */
int  wb_gpu_read_pixel(wb_gpu *g, wb_gpu_texture *tex, float out_rgba[4]);
int  wb_gpu_read_pixel_at(wb_gpu *g, wb_gpu_texture *tex, int x, int y,
                          float out_rgba[4]);

/* True if the backend reports an error. */
int  wb_gpu_ok(const wb_gpu *g);

#ifdef __cplusplus
}
#endif

#endif /* WB_GPU_H */

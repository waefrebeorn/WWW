/*
 * wb_render.h -- Software 3D rasterizer for WuBu Engine (slice 6).
 *
 * The scene path that renders the tile store's world. A minimal fixed-function
 * rasterizer: perspective camera, terrain heightfield mesh, z-buffered
 * triangle fill with flat shading. Pure C11, no deps, works on any framebuffer
 * (VBE on metal, window on the rig). The GPU backend is a later swap behind
 * the same wb_viewport contract -- this is the portable, testable floor.
 *
 * Design:
 *  - Raster into a caller-supplied framebuffer (XRGB8888, y-down).
 *  - A heightfield is described by an opaque accessor: the caller gives WxH
 *    samples and a fn returning (x,y,z) world coords per (sx,sy). The tile
 *    store's payload maps directly onto this per-tile plate; the demo stitches
 *    a plate from resident tiles.
 *  - Camera: look-from, look-at, up, vertical FOV, near/far. World y-up.
 */
#ifndef WB_RENDER_H
#define WB_RENDER_H

#include <stdint.h>
#include <stddef.h>

/* framebuffer */
typedef struct {
    uint32_t *pixels;   /* XRGB8888, row-major, y = 0 at TOP of screen */
    int       width;
    int       height;
} wb_framebuffer;

/* The default clear color: a dark sky. A renderer may use its own. */
#define WB_RENDER_SKY 0x1A2430u

/* camera (world = meters, y-up) -- named wb_render_camera to avoid colliding
 * with the follow/orbit camera in wb_camera.h (both live in src/core). */
typedef struct {
    float eye[3];       /* camera position */
    float at[3];        /* look target */
    float up[3];
    float vfov_deg;     /* vertical field of view */
    float near_z, far_z;
} wb_render_camera;

/* heightfield accessor: world coord of sample (sx,sy) in [0,nx)x[0,ny) */
typedef void (*wb_height_sample_fn)(void *user, int sx, int sy,
                                    float *x, float *y, float *z);

/* world plate drawn this frame (one or many heightfield tiles stitched) */
typedef struct {
    void               *user;
    wb_height_sample_fn sample;   /* world coord per grid sample */
    int                 nx, ny;   /* samples along each axis */
    float               scale;    /* meters between samples */
    float               base[3];  /* world origin of sample (0,0) */
    uint32_t            color;    /* flat base color (XRGB) */
} wb_terrain_plate;

/* Clear the framebuffer to a color. */
void wb_render_clear(wb_framebuffer *fb, uint32_t color);

/* Draw a terrain plate from the camera. Returns face count rasterized. */
int wb_render_terrain(wb_framebuffer *fb, const wb_render_camera *cam,
                      const wb_terrain_plate *plate);

/* Draw a world-space marker: a filled disc at `pos`, `radius_m` across at one
 * metre from the camera, shaded darker with distance so depth still reads.
 *
 * The engine's shipped world holds no terrain -- it is an ECS of bodies -- so
 * this is the primitive that makes "a frame of the world" mean the world's
 * ENTITIES rather than a stand-in landscape. Markers draw over whatever the
 * terrain pass already wrote: the framebuffer exposes no depth buffer, so the
 * entity layer is an overlay, not an occludee. When the world grows terrain the
 * plate goes down first and this stays the layer above it.
 *
 * Returns 1 if the marker was drawn, 0 if it was behind the near plane or off
 * screen. */
int wb_render_marker(wb_framebuffer *fb, const wb_render_camera *cam,
                     const float pos[3], float radius_m, uint32_t color);

#endif /* WB_RENDER_H */

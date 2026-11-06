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

/* camera (world = meters, y-up) */
typedef struct {
    float eye[3];       /* camera position */
    float at[3];        /* look target */
    float up[3];
    float vfov_deg;     /* vertical field of view */
    float near_z, far_z;
} wb_camera;

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
int wb_render_terrain(wb_framebuffer *fb, const wb_camera *cam,
                      const wb_terrain_plate *plate);

#endif /* WB_RENDER_H */

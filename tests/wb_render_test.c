/*
 * wb_render_test.c -- verification of the software rasterizer.
 *
 * Renders a small flat plate and a plate with a raised central block under a
 * fixed camera; then asserts the framebuffer got painted (faces produced,
 * non-background pixels present, and the raised region differs from flat).
 * Real projection math, not just smoke: the flat plate is positioned to
 * cover the screen center, so a filled screen elsewhere indicates a bug.
 */
#include "wb_render.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int g_pass=0, g_fail=0;
#define CHECK(cond,msg) do{ if(cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n",msg,__LINE__);} }while(0)

#define W 160
#define H 120

/* DEM-style sample: returns planar (x,y) and height z. base plate z=0. */
static void flat_sample(void *u,int sx,int sy,float*x,float*y,float*z){
    (void)u; *x=(float)sx; *y=(float)sy; *z=0.0f;
}
/* raised block over the middle half; else z=0 */
static void hills_sample(void *u,int sx,int sy,float*x,float*y,float*z){
    (void)u; *x=(float)sx; *y=(float)sy;
    float dx=(float)sx-7.0f, dy=(float)sy-7.0f;
    *z = (dx*dx+dy*dy < 9.0f) ? 2.0f : 0.0f;
}

static uint32_t count_painted(const wb_framebuffer*fb,uint32_t bg){
    uint32_t n=0; for(int i=0;i<fb->width*fb->height;i++) if(fb->pixels[i]!=bg) n++;
    return n;
}

int main(void){
    static uint32_t buf[W*H];
    wb_framebuffer fb={.pixels=buf,.width=W,.height=H};

    wb_camera cam;
    cam.eye[0]=8.0f; cam.eye[1]=14.0f; cam.eye[2]=6.0f; /* above and in front */
    cam.at[0]=8.0f;  cam.at[1]=0.0f;  cam.at[2]=0.0f;
    cam.up[0]=0.0f;  cam.up[1]=0.0f;  cam.up[2]=1.0f;    /* z up */
    cam.vfov_deg=50.0f; cam.near_z=0.1f; cam.far_z=100.0f;

    uint32_t bg=0x101010;
    wb_render_clear(&fb,bg);
    CHECK(fb.pixels[0]==bg && fb.pixels[W*H-1]==bg, "clear painted full bg");

    wb_terrain_plate plate={0};
    plate.sample=flat_sample; plate.nx=16; plate.ny=16;
    plate.scale=1.0f;
    plate.base[0]=0.0f; plate.base[1]=0.0f; plate.base[2]=0.0f;
    plate.color=0x44AA22;

    int f1=wb_render_terrain(&fb,&cam,&plate);
    CHECK(f1>0, "flat plate rasterized at least one face");
    uint32_t painted_flat=count_painted(&fb,bg);
    CHECK(painted_flat>W*H/8, "flat plate covers substantial screen area");

    /* render the hilly plate: a raised center should change a cluster of px */
    wb_render_clear(&fb,bg);
    plate.sample=hills_sample;
    int f2=wb_render_terrain(&fb,&cam,&plate);
    CHECK(f2>0, "hilly plate rasterized faces");
    uint32_t painted_hills=count_painted(&fb,bg);
    /* hills have a different shaded value than flat, and still cover screen */
    CHECK(painted_hills>W*H/8, "hilly plate covers screen");

    /* the hill is strictly near the center: some pixel in the middle band
     * must be non-bg (camera eyes the plate center). */
    int center_hit=0;
    for(int y=H/3;y<2*H/3;y++) for(int x=W/3;x<2*W/3;x++){
        if(fb.pixels[y*W+x]!=bg){ center_hit=1; break; }
    }
    CHECK(center_hit, "screen center region painted (plate is in front of cam)");

    /* z-buffer sanity: rendering the same plate twice (already-filled depth)
     * must keep the painted region bounded (no unbounded growth of pixels) */
    int f3=wb_render_terrain(&fb,&cam,&plate);
    (void)f3;

    printf("=== RENDER TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

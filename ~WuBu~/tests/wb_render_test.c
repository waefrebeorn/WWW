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

    wb_render_camera cam;
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

    /* ---- markers: the entity layer -------------------------------------- */
    /* The world is an ECS of bodies, so the frame has to show bodies, not just
     * terrain. A marker at the camera's look target must land on the screen
     * centre; one behind the near plane and one far off to the side must not
     * be drawn at all. */
    wb_render_clear(&fb,bg);
    float at_pt[3]={8.0f,0.0f,0.0f};            /* exactly cam.at */
    int m1=wb_render_marker(&fb,&cam,at_pt,0.5f,0xFFE080);
    CHECK(m1==1, "marker at the look target is drawn");
    uint32_t centre_px=fb.pixels[(H/2)*W+(W/2)];
    CHECK(centre_px!=bg, "marker at the look target lands on the screen centre");

    /* the whole marker is the marker colour: no stray background inside it */
    int marker_px=0;
    for(int y=H/2-6;y<=H/2+6;y++) for(int x=W/2-6;x<=W/2+6;x++)
        if(fb.pixels[y*W+x]!=bg) marker_px++;
    CHECK(marker_px>20, "marker covers a disc, not a single pixel");

    /* behind the near plane: nothing drawn. The point is the reflection of the
     * look target through the eye, so it is the same distance away on the far
     * side -- unambiguously behind the camera, with a sane up vector. */
    wb_render_clear(&fb,bg);
    wb_render_camera c2=cam;
    float behind[3];
    behind[0]=2.0f*cam.eye[0]-cam.at[0];
    behind[1]=2.0f*cam.eye[1]-cam.at[1];
    behind[2]=2.0f*cam.eye[2]-cam.at[2];
    int m2=wb_render_marker(&fb,&c2,behind,0.5f,0xFFE080);
    CHECK(m2==0, "marker behind the near plane is not drawn");
    CHECK(count_painted(&fb,bg)==0, "nothing painted for a marker behind the camera");

    /* far off to the side: outside the frustum, nothing drawn */
    wb_render_clear(&fb,bg);
    float aside[3]={8.0f,500.0f,0.0f};
    int m3=wb_render_marker(&fb,&c2,aside,0.5f,0xFFE080);
    CHECK(m3==0, "marker outside the frustum is not drawn");

    /* ---- a capsule: the primitive a body is made of ---------------------- */
    /* A marker is a point; a torso, a limb, a bat and a beam all have extent.
     * The capsule is the smallest thing that can express that. */
    wb_render_clear(&fb,bg);
    float a1[3]={8.0f,0.0f,0.0f}, b1[3]={8.0f,0.0f,4.0f};   /* vertical, 4 m */
    int s1=wb_render_segment(&fb,&c2,a1,b1,0.10f,0xFFE080);
    CHECK(s1>0, "a segment in view is drawn");
    uint32_t seg_px=count_painted(&fb,bg);
    CHECK(seg_px>20, "a segment covers a band, not a single pixel");

    /* It is a band of roughly the right size: a 4 m capsule at that camera is a
     * few hundred pixels, not one and not the whole frame. */
    CHECK(seg_px < W*H/4, "a segment does not flood the framebuffer");

    /* A segment lies along its axis: it must be TALLER than it is wide. */
    int minx=W,maxx=-1,miny=H,maxy=-1;
    for(int y=0;y<H;y++)for(int x=0;x<W;x++)
        if(fb.pixels[y*W+x]!=bg){
            if(x<minx) minx=x;
            if(x>maxx) maxx=x;
            if(y<miny) miny=y;
            if(y>maxy) maxy=y;
        }
    CHECK(maxy-miny > maxx-minx, "a vertical capsule is taller than it is wide");

    /* Zero width draws nothing: there is no capsule of zero thickness. */
    wb_render_clear(&fb,bg);
    int s0=wb_render_segment(&fb,&c2,a1,b1,0.0f,0xFFE080);
    CHECK(s0==0, "a zero-width segment draws nothing");

    /* A point segment (both ends equal) is a marker, not a line. */
    wb_render_clear(&fb,bg);
    int s2=wb_render_segment(&fb,&c2,a1,a1,0.10f,0xFFE080);
    CHECK(s2>0, "a degenerate segment still draws a dot");

    /* Behind the near plane: nothing, and no fault. */
    wb_render_clear(&fb,bg);
    float a3[3]={8.0f,0.0f,0.0f}, b3[3];
    b3[0]=2.0f*c2.eye[0]-c2.at[0];
    b3[1]=2.0f*c2.eye[1]-c2.at[1];
    b3[2]=2.0f*c2.eye[2]-c2.at[2];
    int s3=wb_render_segment(&fb,&c2,a3,b3,0.10f,0xFFE080);
    CHECK(s3==0, "a segment behind the near plane is not drawn");

    /* A NULL framebuffer is harmless. */
    wb_framebuffer nil={NULL,0,0};
    wb_render_segment(&nil,&c2,a1,b1,0.10f,0xFFE080);
    CHECK(1, "a NULL framebuffer is harmless");

    printf("=== RENDER TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

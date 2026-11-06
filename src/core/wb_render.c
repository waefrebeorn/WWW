/*
 * wb_render.c -- fixed-function software rasterizer.
 *
 * Perspective pipeline: world -> view (LookAt) -> clip -> NDC -> screen.
 * Terrain grid is drawn as 2 triangles/cell, filled by edge-function
 * rasterization with a z-buffer (barycentric-interpolated view-space depth)
 * for correct occlusion. Flat shading: brightness from normal y (top light)
 * so relief shows. Pure C11, works on any framebuffer.
 */
#include "wb_render.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

typedef struct { float v[4]; } vec4;
typedef struct { float m[4][4]; } mat4;

static void m4_lookat(mat4 *out, const float eye[3], const float at[3], const float up[3]) {
    float f[3], s[3], u[3], z[3];
    f[0]=at[0]-eye[0]; f[1]=at[1]-eye[1]; f[2]=at[2]-eye[2];
    float l=sqrtf(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]); if(l>0){f[0]/=l;f[1]/=l;f[2]/=l;}
    s[0]=f[1]*up[2]-f[2]*up[1]; s[1]=f[2]*up[0]-f[0]*up[2]; s[2]=f[0]*up[1]-f[1]*up[0];
    l=sqrtf(s[0]*s[0]+s[1]*s[1]+s[2]*s[2]); if(l>0){s[0]/=l;s[1]/=l;s[2]/=l;}
    u[0]=s[1]*f[2]-s[2]*f[1]; u[1]=s[2]*f[0]-s[0]*f[2]; u[2]=s[0]*f[1]-s[1]*f[0];
    z[0]=-f[0]; z[1]=-f[1]; z[2]=-f[2];
    memset(out->m,0,sizeof(*out));
    out->m[0][0]=s[0]; out->m[0][1]=s[1]; out->m[0][2]=s[2];
    out->m[1][0]=u[0]; out->m[1][1]=u[1]; out->m[1][2]=u[2];
    out->m[2][0]=z[0]; out->m[2][1]=z[1]; out->m[2][2]=z[2];
    out->m[0][3]=-(s[0]*eye[0]+s[1]*eye[1]+s[2]*eye[2]);
    out->m[1][3]=-(u[0]*eye[0]+u[1]*eye[1]+u[2]*eye[2]);
    out->m[2][3]=-(z[0]*eye[0]+z[1]*eye[1]+z[2]*eye[2]);
    out->m[3][3]=1.0f;
}

static void m4_perspective(mat4 *out, float vfov_deg, float aspect, float near, float far) {
    float f = 1.0f / tanf(vfov_deg * (3.14159265f/180.0f) * 0.5f);
    memset(out->m,0,sizeof(*out));
    out->m[0][0]=f/aspect; out->m[1][1]=f;
    out->m[2][2]=(far+near)/(near-far);
    out->m[2][3]=(2.0f*far*near)/(near-far);
    out->m[3][2]=-1.0f;
}

static vec4 mulv(const mat4 *m, float x, float y, float z, float w) {
    vec4 o;
    o.v[0]=m->m[0][0]*x+m->m[0][1]*y+m->m[0][2]*z+m->m[0][3]*w;
    o.v[1]=m->m[1][0]*x+m->m[1][1]*y+m->m[1][2]*z+m->m[1][3]*w;
    o.v[2]=m->m[2][0]*x+m->m[2][1]*y+m->m[2][2]*z+m->m[2][3]*w;
    o.v[3]=m->m[3][0]*x+m->m[3][1]*y+m->m[3][2]*z+m->m[3][3]*w;
    return o;
}

static float edgef(float ax,float ay,float bx,float by,float px,float py){
    return (px-ax)*(by-ay)-(py-ay)*(bx-ax);
}

static uint32_t shade(uint32_t c, float b) {
    if (b<0.0f) { b=0.0f; }
    if (b>1.0f) { b=1.0f; }
    uint32_t r=(c>>16)&0xFF, g=(c>>8)&0xFF, bl=c&0xFF;
    return ((uint32_t)(r*b)<<16)|((uint32_t)(g*b)<<8)|(uint32_t)(bl*b);
}
void wb_render_clear(wb_framebuffer *fb, uint32_t color) {
    uint32_t n=(uint32_t)fb->width*(uint32_t)fb->height;
    for (uint32_t i=0;i<n;i++) fb->pixels[i]=color;
}

/* one terrain plate's sample of world coordinate. Samples are grid 0..nx-1
 * with spacing 'scale' horizontally/vertically, height comes from the
 * caller's sample() signature (x,y,z filled; z is the height). We treat the
 * grid's (x,y) as the horizontal plane and z as altitude for a land plate. */
int wb_render_terrain(wb_framebuffer *fb, const wb_camera *cam,
                      const wb_terrain_plate *p) {
    int W=fb->width, H=fb->height, faces=0;
    size_t zbuf_n=(size_t)W*(size_t)H;
    float *zb=malloc(zbuf_n*sizeof(float));
    if(!zb) return 0;
    /* depth buffer must start at +INF so every first-write to a pixel wins;
     * view depths are positive and can exceed 1.0 (far plane scale). */
    for(size_t i=0;i<zbuf_n;i++) zb[i]=INFINITY;

    mat4 view, proj;
    m4_lookat(&view, cam->eye, cam->at, cam->up);
    m4_perspective(&proj, cam->vfov_deg, (float)W/(float)H, cam->near_z, cam->far_z);

    int nx=p->nx, ny=p->ny;
    for (int sy=0; sy<ny-1; sy++) for (int sx=0; sx<nx-1; sx++) {
        int tris[2][3]={
            { sy*nx+sx, sy*nx+sx+1, (sy+1)*nx+sx+1 },
            { sy*nx+sx, (sy+1)*nx+sx+1, (sy+1)*nx+sx },
        };
        for (int t=0;t<2;t++) {
            vec4 wv[3], vv[3]; float px[3],py[3],pz[3];
            int reject=0;
            for (int k=0;k<3;k++) {
                int sxi=tris[t][k]%nx, syk=tris[t][k]/nx;
                float hx,hy,hz;
                p->sample(p->user, sxi, syk, &hx,&hy,&hz);
                /* DEM semantics: sample() returns planar (x,y) + height(z);
                 * drop height onto world up (z). */
                float wx=p->base[0]+hx, wy=p->base[1]+hy, wz=p->base[2]+hz;
                wv[k].v[0]=wx; wv[k].v[1]=wy; wv[k].v[2]=wz; wv[k].v[3]=1.0f;
                vv[k]=mulv(&view, wx,wy,wz, 1.0f);
                if (vv[k].v[2] > -cam->near_z) reject=1;   /* behind near plane */
            }
            if (reject) continue;

            /* project to screen */
            for (int k=0;k<3;k++) {
                vec4 c= mulv(&proj, vv[k].v[0], vv[k].v[1], vv[k].v[2], vv[k].v[3]);
                if (c.v[3] <= 1e-6f) { reject=1; break; }
                float w=c.v[3];
                px[k]=(c.v[0]/w*0.5f+0.5f)*(float)W;
                py[k]=(0.5f-c.v[1]/w*0.5f)*(float)H;
                pz[k]=-vv[k].v[2];          /* view depth, positive near->far */
            }
            if (reject) continue;

            /* face normal + top light */
            vec4 A=wv[0],B=wv[1],C=wv[2];
            float nxw=(B.v[1]-A.v[1])*(C.v[2]-A.v[2])-(B.v[2]-A.v[2])*(C.v[1]-A.v[1]);
            float nyw=(B.v[2]-A.v[2])*(C.v[0]-A.v[0])-(B.v[0]-A.v[0])*(C.v[2]-A.v[2]);
            float nzw=(B.v[0]-A.v[0])*(C.v[1]-A.v[1])-(B.v[1]-A.v[1])*(C.v[0]-A.v[0]);
            float nl=sqrtf(nxw*nxw+nyw*nyw+nzw*nzw); if(nl>0){nxw/=nl;nyw/=nl;nzw/=nl;}
            float light=fabsf(nyw)*0.7f+0.3f;
            uint32_t col=shade(p->color, light);

            int minx=(int)floorf(fminf(fminf(px[0],px[1]),px[2]));
            int maxx=(int) ceilf(fmaxf(fmaxf(px[0],px[1]),px[2]));
            int miny=(int)floorf(fminf(fminf(py[0],py[1]),py[2]));
            int maxy=(int) ceilf(fmaxf(fmaxf(py[0],py[1]),py[2]));
            if(minx<0)minx=0;
            if(miny<0)miny=0;
            if(maxx>W)maxx=W;
            if(maxy>H)maxy=H;

            /* projected screen y is DOWN, so world-CCW triangles come out CW
             * here: all edge functions are negative for inside points. flip
             * the inside test by the projected triangle's signed area. */
            float ori = edgef(px[0],py[0],px[1],py[1],px[2],py[2]);
            float esign = (ori < 0.0f) ? -1.0f : 1.0f;

            for (int y=miny;y<maxy;y++) for (int x=minx;x<maxx;x++) {
                float pf=(float)x+0.5f, qf=(float)y+0.5f;
                float e0=edgef(px[0],py[0],px[1],py[1],pf,qf)*esign;
                float e1=edgef(px[1],py[1],px[2],py[2],pf,qf)*esign;
                float e2=edgef(px[2],py[2],px[0],py[0],pf,qf)*esign;
                if (e0>=0&&e1>=0&&e2>=0) {
                    float eS=e0+e1+e2; if(eS<=0)continue;
                    /* barycentric depth: l0,v0  l1,v1  l2,v2 */
                    float z=(e1*pz[0]+e2*pz[1]+e0*pz[2])/eS;
                    size_t fi=(size_t)y*(size_t)W+(size_t)x;
                    if (fi<zbuf_n && z<zb[fi]) { zb[fi]=z; fb->pixels[fi]=col; }
                }
            }
            faces++;
        }
    }
    free(zb);
    return faces;
}

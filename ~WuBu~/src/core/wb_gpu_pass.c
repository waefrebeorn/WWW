/*
 * wb_gpu_pass.c -- render-graph pass recording: declare targets, reads and
 * draws. See wb_gpu.h.
 */
#include "wb_gpu_internal.h"

#include <stdio.h>
#include <string.h>

wb_gpu_pass *wb_gpu_pass_begin(wb_gpu *g, const char *name) {
    if (!g || g->npasses >= WB_GPU_MAX_PASSES) return NULL;
    wb_pass_impl *p = &g->passes[g->npasses];
    memset(p, 0, sizeof *p);
    snprintf(p->name, sizeof p->name, "%s", name ? name : "pass");
    g->npasses++;                 /* the pass occupies this slot now */
    g->recording = 1;
    return (wb_gpu_pass *)p;
}

void wb_gpu_pass_set_color(wb_gpu_pass *pass, int slot, wb_gpu_texture *tex,
                           const float clear_rgba[4]) {
    if (!pass || slot < 0 || slot >= 4) return;
    wb_pass_impl *p = (wb_pass_impl *)pass;
    p->color[slot] = tex;
    p->clear_color[slot] = clear_rgba != NULL;
    if (clear_rgba) memcpy(p->clear[slot], clear_rgba, 4 * sizeof(float));
    p->ncolor++;
}

void wb_gpu_pass_set_depth(wb_gpu_pass *pass, wb_gpu_texture *tex,
                           int clear, float depth, uint8_t stencil) {
    if (!pass) return;
    wb_pass_impl *p = (wb_pass_impl *)pass;
    p->depth = tex;
    p->clear_depth = clear;
    p->depth_val = depth;
    (void)stencil;
}

void wb_gpu_pass_read(wb_gpu_pass *pass, wb_gpu_texture *tex, int slot) {
    if (!pass || slot < 0 || slot >= 8) return;
    wb_pass_impl *p = (wb_pass_impl *)pass;
    p->reads[p->nreads++] = tex;
}

void wb_gpu_pass_draw(wb_gpu_pass *pass, wb_gpu_pipeline *pipe,
                      wb_gpu_buffer *vb, wb_gpu_buffer *ib,
                      int vertex_count, int index_count,
                      int instance_count, int first_vertex) {
    if (!pass) return;
    wb_pass_impl *p = (wb_pass_impl *)pass;
    if (p->ndraws >= (int)(sizeof p->draws / sizeof p->draws[0])) return;
    p->draws[p->ndraws].pipe = pipe;
    p->draws[p->ndraws].vb = vb;
    p->draws[p->ndraws].ib = ib;
    p->draws[p->ndraws].vc = vertex_count;
    p->draws[p->ndraws].ic = index_count;
    p->draws[p->ndraws].inst = instance_count > 0 ? instance_count : 1;
    p->draws[p->ndraws].first = first_vertex;
    p->ndraws++;
}

void wb_gpu_pass_end(wb_gpu_pass *pass) {
    /* the pass is already stored in g->passes by pass_begin; nothing to do */
    (void)pass;
}

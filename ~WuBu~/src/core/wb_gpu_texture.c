/*
 * wb_gpu_texture.c -- texture creation, descriptors and upload. See wb_gpu.h.
 */
#include "wb_gpu_internal.h"

#include <stdlib.h>
#include <string.h>

wb_gpu_texture *wb_gpu_texture_create(wb_gpu *g, wb_gpu_format fmt,
                                      int width, int height, const void *initial) {
    if (!g || width <= 0 || height <= 0) return NULL;
    wb_gpu_texture *t = (wb_gpu_texture *)calloc(1, sizeof *t);
    if (!t) return NULL;
    t->w = width; t->h = height; t->format = fmt; t->fmt = wb_gpu_dxgi_format(fmt);
    t->state = WB_USE_NONE;

    D3D12_HEAP_PROPERTIES hp;
    memset(&hp, 0, sizeof hp);
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC rd;
    memset(&rd, 0, sizeof rd);
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = (UINT64)width;
    rd.Height = (UINT)height;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = t->fmt;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    D3D12_CLEAR_VALUE cv;
    memset(&cv, 0, sizeof cv);
    cv.Format = t->fmt;
    D3D12_CLEAR_VALUE *pcv = NULL;
    if (wb_gpu_is_depth_format(fmt)) { cv.DepthStencil.Depth = 1.0f; cv.DepthStencil.Stencil = 0; pcv = &cv; }
    else { cv.Color[0]=cv.Color[1]=cv.Color[2]=cv.Color[3]=0.0f; pcv = &cv; }

    D3D12_RESOURCE_STATES st = wb_gpu_is_depth_format(fmt) ? D3D12_RESOURCE_STATE_DEPTH_WRITE
                                                          : D3D12_RESOURCE_STATE_COPY_DEST;
    rd.Flags = wb_gpu_is_depth_format(fmt) ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
                                           : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    HRESULT hr = g->dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, st, pcv,
                                                 IID_PPV_ARGS(&t->res));
    if (wb_gpu_failed(hr)) { wb_gpu_set_err(g, "texture resource failed"); free(t); return NULL; }
    t->state = wb_gpu_is_depth_format(fmt) ? WB_USE_DEPTH : WB_USE_RENDER_TARGET;

    /* descriptors */
    if (!wb_gpu_is_depth_format(fmt)) {
        D3D12_CPU_DESCRIPTOR_HANDLE h = g->rtv_heap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += (size_t)g->rtv_used * g->rtv_sz;
        g->dev->CreateRenderTargetView(t->res, NULL, h);
        t->rtv = h; t->has_rtv = 1;
        g->rtv_used++;

        D3D12_CPU_DESCRIPTOR_HANDLE s = g->srv_heap->GetCPUDescriptorHandleForHeapStart();
        s.ptr += (size_t)g->srv_used * g->srv_sz;
        D3D12_SHADER_RESOURCE_VIEW_DESC sd;
        memset(&sd, 0, sizeof sd);
        sd.Format = t->fmt;
        sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Texture2D.MipLevels = 1;
        g->dev->CreateShaderResourceView(t->res, &sd, s);
        t->srv = s; t->has_srv = 1;
        t->srv_index = g->srv_used;
        g->srv_used++;
    } else {
        D3D12_CPU_DESCRIPTOR_HANDLE h = g->dsv_heap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += (size_t)g->dsv_used * g->dsv_sz;
        g->dev->CreateDepthStencilView(t->res, NULL, h);
        t->dsv = h; t->has_dsv = 1;
        g->dsv_used++;
    }

    if (initial) {
        /* upload through a buffer */
        size_t row = (size_t)width * 4;
        size_t bytes = row * (size_t)height;
        ID3D12Resource *up = wb_gpu_make_buffer_resource(g, bytes, D3D12_HEAP_TYPE_UPLOAD,
                                                         D3D12_RESOURCE_STATE_GENERIC_READ);
        if (up) {
            void *m = NULL;
            up->Map(0, NULL, &m);
            memcpy(m, initial, bytes);
            up->Unmap(0, NULL);
            g->alloc->Reset();
            g->list->Reset(g->alloc, NULL);
            D3D12_TEXTURE_COPY_LOCATION src, dst;
            memset(&src, 0, sizeof src); memset(&dst, 0, sizeof dst);
            src.pResource = up; src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint.Offset = 0;
            src.PlacedFootprint.Footprint.Format = t->fmt;
            src.PlacedFootprint.Footprint.Width = (UINT)width;
            src.PlacedFootprint.Footprint.Height = (UINT)height;
            src.PlacedFootprint.Footprint.Depth = 1;
            src.PlacedFootprint.Footprint.RowPitch = (UINT)row;
            dst.pResource = t->res; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.SubresourceIndex = 0;
            g->list->CopyTextureRegion(&dst, 0, 0, 0, &src, NULL);
            D3D12_RESOURCE_BARRIER bar;
            memset(&bar, 0, sizeof bar);
            bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            bar.Transition.pResource = t->res;
            bar.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            bar.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            g->list->ResourceBarrier(1, &bar);
            g->list->Close();
            ID3D12CommandList *lists[1] = { g->list };
            g->queue->ExecuteCommandLists(1, lists);
            g->fence_value++;
            g->queue->Signal(g->fence, g->fence_value);
            if (g->fence->GetCompletedValue() < g->fence_value) {
                g->fence->SetEventOnCompletion(g->fence_value, g->fence_event);
                WaitForSingleObject(g->fence_event, 1000);
            }
            up->Release();
            t->state = WB_USE_SHADER_RESOURCE;
        }
    }
    return t;
}

void wb_gpu_texture_destroy(wb_gpu_texture *t) {
    if (!t) return;
    if (t->res) t->res->Release();
    free(t->backing);
    free(t);
}

int wb_gpu_texture_width(const wb_gpu_texture *t) { return t ? t->w : 0; }
int wb_gpu_texture_height(const wb_gpu_texture *t) { return t ? t->h : 0; }
wb_gpu_format wb_gpu_texture_format(const wb_gpu_texture *t) { return t ? t->format : WB_GPU_FMT_RGBA8; }

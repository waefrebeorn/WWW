/*
 * wb_gpu_exec.c -- graph execution: barriers derived from declared usage,
 * command recording, submission, readback and present. See wb_gpu.h.
 */
#include "wb_gpu_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int wb_gpu_execute(wb_gpu *g) {
    if (!g) return -1;
    g->err[0] = 0;

    g->alloc->Reset();
    HRESULT hr = g->list->Reset(g->alloc, NULL);
    if (wb_gpu_failed(hr)) { wb_gpu_set_err(g, "command list reset failed"); return -1; }

    ID3D12DescriptorHeap *heaps[1] = { g->srv_heap };
    g->list->SetDescriptorHeaps(1, heaps);
    g->list->SetGraphicsRootSignature(g->root_sig);

    for (int i = 0; i < g->npasses; i++) {
        wb_pass_impl *p = &g->passes[i];

        /* --- barriers: transition this pass's resources to their use --- */
        for (int c = 0; c < 4; c++) {
            wb_gpu_texture *t = p->color[c];
            if (!t) continue;
            if (t->state != WB_USE_RENDER_TARGET) {
                D3D12_RESOURCE_BARRIER bar;
                memset(&bar, 0, sizeof bar);
                bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                bar.Transition.pResource = t->res;
                bar.Transition.StateBefore = wb_gpu_state_for(t->state);
                bar.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
                bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                g->list->ResourceBarrier(1, &bar);
                t->state = WB_USE_RENDER_TARGET;
            }
        }
        if (p->depth) {
            wb_gpu_texture *t = p->depth;
            if (t->state != WB_USE_DEPTH) {
                D3D12_RESOURCE_BARRIER bar;
                memset(&bar, 0, sizeof bar);
                bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                bar.Transition.pResource = t->res;
                bar.Transition.StateBefore = wb_gpu_state_for(t->state);
                bar.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
                bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                g->list->ResourceBarrier(1, &bar);
                t->state = WB_USE_DEPTH;
            }
        }
        for (int r = 0; r < p->nreads; r++) {
            wb_gpu_texture *t = p->reads[r];
            if (!t) continue;
            if (t->state != WB_USE_SHADER_RESOURCE) {
                D3D12_RESOURCE_BARRIER bar;
                memset(&bar, 0, sizeof bar);
                bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                bar.Transition.pResource = t->res;
                bar.Transition.StateBefore = wb_gpu_state_for(t->state);
                bar.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                g->list->ResourceBarrier(1, &bar);
                t->state = WB_USE_SHADER_RESOURCE;
            }
        }

        /* --- render pass begin --- */
        D3D12_CPU_DESCRIPTOR_HANDLE rtvh[4];
        int nrtv = 0;
        for (int c = 0; c < 4; c++) {
            if (!p->color[c]) continue;
            rtvh[nrtv++] = p->color[c]->rtv;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE dsvh = {};
        if (p->depth) dsvh = p->depth->dsv;

        if (nrtv > 0 || p->depth) {
            g->list->OMSetRenderTargets((UINT)nrtv, nrtv ? rtvh : NULL, FALSE,
                                        p->depth ? &dsvh : NULL);
        }
        for (int c = 0; c < 4; c++) {
            if (p->color[c] && p->clear_color[c])
                g->list->ClearRenderTargetView(p->color[c]->rtv, p->clear[c], 0, NULL);
        }
        if (p->depth && p->clear_depth)
            g->list->ClearDepthStencilView(p->depth->dsv,
                D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
                p->depth_val, 0, 0, NULL);

        D3D12_VIEWPORT vp;
        vp.TopLeftX = 0; vp.TopLeftY = 0;
        vp.Width = (float)(p->color[0] ? p->color[0]->w : g->swap_w);
        vp.Height = (float)(p->color[0] ? p->color[0]->h : g->swap_h);
        vp.MinDepth = 0.0f; vp.MaxDepth = 1.0f;
        g->list->RSSetViewports(1, &vp);
        D3D12_RECT sc = { 0, 0, (LONG)vp.Width, (LONG)vp.Height };
        g->list->RSSetScissorRects(1, &sc);

        /* --- draws --- */
        /* Bind the first declared texture as SRV t0 (parameter index 1). */
        if (p->nreads > 0 && p->reads[0] && p->reads[0]->has_srv) {
            D3D12_GPU_DESCRIPTOR_HANDLE gh = g->srv_heap->GetGPUDescriptorHandleForHeapStart();
            gh.ptr += (size_t)p->reads[0]->srv_index * g->srv_sz;
            g->list->SetGraphicsRootDescriptorTable(1, gh);
        }
        for (int d = 0; d < p->ndraws; d++) {
            wb_draw_cmd *dr = &p->draws[d];
            if (dr->pipe && dr->pipe->pso) g->list->SetPipelineState(dr->pipe->pso);
            g->list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            if (dr->vb) {
                D3D12_VERTEX_BUFFER_VIEW vb;
                vb.BufferLocation = dr->vb->res->GetGPUVirtualAddress();
                vb.SizeInBytes = (UINT)dr->vb->size;
                vb.StrideInBytes = 24;   /* float3 pos + float3 normal */
                g->list->IASetVertexBuffers(0, 1, &vb);
            }
            if (dr->ib) {
                D3D12_INDEX_BUFFER_VIEW ib;
                ib.BufferLocation = dr->ib->res->GetGPUVirtualAddress();
                ib.SizeInBytes = (UINT)dr->ib->size;
                ib.Format = DXGI_FORMAT_R32_UINT;
                g->list->IASetIndexBuffer(&ib);
                g->list->DrawIndexedInstanced((UINT)dr->ic, (UINT)dr->inst, 0, (UINT)dr->first, 0);
            } else {
                g->list->DrawInstanced((UINT)dr->vc, (UINT)dr->inst, (UINT)dr->first, 0);
            }
        }
    }

    g->list->Close();
    ID3D12CommandList *lists[1] = { g->list };
    g->queue->ExecuteCommandLists(1, lists);

    g->fence_value++;
    g->queue->Signal(g->fence, g->fence_value);
    if (g->fence->GetCompletedValue() < g->fence_value) {
        g->fence->SetEventOnCompletion(g->fence_value, g->fence_event);
        WaitForSingleObject(g->fence_event, 2000);
    }

    /* reset the graph for the next frame */
    g->npasses = 0;
    return 0;
}

int wb_gpu_read_pixel_at(wb_gpu *g, wb_gpu_texture *tex, int x, int y,
                         float out_rgba[4]) {
    if (!g || !tex || !tex->res) return -1;
    if (x < 0 || y < 0 || x >= tex->w || y >= tex->h) return -1;
    size_t row = (size_t)tex->w * 4;
    size_t bytes = row * (size_t)tex->h;
    wb_gpu_buffer *rb = wb_gpu_buffer_create(g, WB_GPU_BUF_READBACK, bytes, NULL);
    if (!rb) return -1;

    D3D12_RESOURCE_BARRIER bar;
    memset(&bar, 0, sizeof bar);
    bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    bar.Transition.pResource = tex->res;
    bar.Transition.StateBefore = wb_gpu_state_for(tex->state);
    bar.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    g->alloc->Reset();
    g->list->Reset(g->alloc, NULL);
    g->list->ResourceBarrier(1, &bar);
    D3D12_TEXTURE_COPY_LOCATION src, dst;
    memset(&src, 0, sizeof src); memset(&dst, 0, sizeof dst);
    src.pResource = tex->res; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    dst.pResource = rb->res; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Offset = 0;
    dst.PlacedFootprint.Footprint.Format = tex->fmt;
    dst.PlacedFootprint.Footprint.Width = (UINT)tex->w;
    dst.PlacedFootprint.Footprint.Height = (UINT)tex->h;
    dst.PlacedFootprint.Footprint.Depth = 1;
    dst.PlacedFootprint.Footprint.RowPitch = (UINT)row;
    g->list->CopyTextureRegion(&dst, 0, 0, 0, &src, NULL);
    bar.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    bar.Transition.StateAfter = wb_gpu_state_for(tex->state);
    g->list->ResourceBarrier(1, &bar);
    g->list->Close();
    ID3D12CommandList *lists[1] = { g->list };
    g->queue->ExecuteCommandLists(1, lists);
    g->fence_value++;
    g->queue->Signal(g->fence, g->fence_value);
    if (g->fence->GetCompletedValue() < g->fence_value) {
        g->fence->SetEventOnCompletion(g->fence_value, g->fence_event);
        WaitForSingleObject(g->fence_event, 2000);
    }

    void *m = NULL;
    D3D12_RANGE r = {0, bytes};
    if (wb_gpu_failed(rb->res->Map(0, &r, &m))) { wb_gpu_buffer_destroy(rb); return -1; }
    const unsigned char *px = (const unsigned char *)m + (size_t)y * row + (size_t)x * 4;
    out_rgba[0] = px[0] / 255.0f;
    out_rgba[1] = px[1] / 255.0f;
    out_rgba[2] = px[2] / 255.0f;
    out_rgba[3] = px[3] / 255.0f;
    rb->res->Unmap(0, NULL);
    wb_gpu_buffer_destroy(rb);
    return 0;
}

int wb_gpu_read_pixel(wb_gpu *g, wb_gpu_texture *tex, float out_rgba[4]) {
    return wb_gpu_read_pixel_at(g, tex, 0, 0, out_rgba);
}

int wb_gpu_execute_present(wb_gpu *g, void *hwnd) {
    if (!g || !hwnd) return -1;
    if (!g->swap) {
        DXGI_SWAP_CHAIN_DESC1 sd;
        memset(&sd, 0, sizeof sd);
        sd.Width = g->swap_w ? g->swap_w : 1280;
        sd.Height = g->swap_h ? g->swap_h : 720;
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        IDXGISwapChain1 *sc1 = NULL;
        HRESULT hr = g->factory->CreateSwapChainForHwnd((IUnknown *)g->queue, (HWND)hwnd,
                                                       &sd, NULL, NULL, &sc1);
        if (wb_gpu_failed(hr)) { wb_gpu_set_err(g, "CreateSwapChainForHwnd failed"); return -1; }
        if (wb_gpu_failed(sc1->QueryInterface(IID_PPV_ARGS(&g->swap)))) {
            wb_gpu_set_err(g, "swapchain query failed"); sc1->Release(); return -1;
        }
        sc1->Release();
        g->swap_w = sd.Width; g->swap_h = sd.Height;
    }
    g->swap_back = g->swap->GetCurrentBackBufferIndex();
    if (!g->swap_rtv[0]) {
        for (int i = 0; i < 2; i++) {
            ID3D12Resource *buf = NULL;
            g->swap->GetBuffer((UINT)i, IID_PPV_ARGS(&buf));
            D3D12_CPU_DESCRIPTOR_HANDLE h = g->rtv_heap->GetCPUDescriptorHandleForHeapStart();
            h.ptr += (size_t)g->rtv_used * g->rtv_sz;
            g->dev->CreateRenderTargetView(buf, NULL, h);
            wb_gpu_texture *t = (wb_gpu_texture *)calloc(1, sizeof *t);
            t->res = buf; t->fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
            t->w = g->swap_w; t->h = g->swap_h; t->format = WB_GPU_FMT_RGBA8;
            t->rtv = h; t->has_rtv = 1; t->state = WB_USE_RENDER_TARGET;
            g->swap_rtv[i] = t;
            g->rtv_used++;
        }
    }
    /* run the graph with the back buffer as color target 0 */
    int rc = wb_gpu_execute(g);
    if (rc != 0) return rc;
    g->swap->Present(1, 0);
    return 0;
}

/*
 * wb_gpu_buffer.c -- GPU buffers: creation, upload, map/unmap, readback.
 * See wb_gpu.h.
 */
#include "wb_gpu_internal.h"

#include <stdlib.h>
#include <string.h>

ID3D12Resource *wb_gpu_make_buffer_resource(wb_gpu *g, size_t size,
                                            D3D12_HEAP_TYPE heap,
                                            D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES hp;
    memset(&hp, 0, sizeof hp);
    hp.Type = heap;
    D3D12_RESOURCE_DESC rd;
    memset(&rd, 0, sizeof rd);
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = (UINT64)size;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource *res = NULL;
    g->dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, state, NULL,
                                    IID_PPV_ARGS(&res));
    return res;
}

wb_gpu_buffer *wb_gpu_buffer_create(wb_gpu *g, wb_gpu_buffer_kind kind,
                                    size_t size, const void *initial) {
    if (!g || size == 0) return NULL;
    wb_gpu_buffer *b = (wb_gpu_buffer *)calloc(1, sizeof *b);
    if (!b) return NULL;
    b->size = size;
    b->kind = kind;

    if (kind == WB_GPU_BUF_READBACK) {
        b->res = wb_gpu_make_buffer_resource(g, size, D3D12_HEAP_TYPE_READBACK,
                                             D3D12_RESOURCE_STATE_COPY_DEST);
        if (!b->res) { wb_gpu_set_err(g, "readback buffer failed"); free(b); return NULL; }
        b->cpu_copy = (UINT8 *)calloc(1, size);
        return b;
    }

    b->res = wb_gpu_make_buffer_resource(g, size, D3D12_HEAP_TYPE_DEFAULT,
                                         D3D12_RESOURCE_STATE_COPY_DEST);
    if (!b->res) { wb_gpu_set_err(g, "buffer resource failed"); free(b); return NULL; }

    if (initial) {
        ID3D12Resource *up = wb_gpu_make_buffer_resource(g, size, D3D12_HEAP_TYPE_UPLOAD,
                                                         D3D12_RESOURCE_STATE_GENERIC_READ);
        if (!up) { wb_gpu_set_err(g, "upload buffer failed"); wb_gpu_buffer_destroy(b); return NULL; }
        void *m = NULL;
        up->Map(0, NULL, &m);
        memcpy(m, initial, size);
        up->Unmap(0, NULL);

        g->alloc->Reset();
        g->list->Reset(g->alloc, NULL);
        g->list->CopyResource(b->res, up);
        D3D12_RESOURCE_BARRIER bar;
        memset(&bar, 0, sizeof bar);
        bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        bar.Transition.pResource = b->res;
        bar.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        bar.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        g->list->ResourceBarrier(1, &bar);
        g->list->Close();
        ID3D12CommandList *lists[1] = { g->list };
        g->queue->ExecuteCommandLists(1, lists);
        /* small fence wait so the upload is not freed before completion */
        g->fence_value++;
        g->queue->Signal(g->fence, g->fence_value);
        if (g->fence->GetCompletedValue() < g->fence_value) {
            g->fence->SetEventOnCompletion(g->fence_value, g->fence_event);
            WaitForSingleObject(g->fence_event, 1000);
        }
        up->Release();
    }
    return b;
}

void wb_gpu_buffer_destroy(wb_gpu_buffer *b) {
    if (!b) return;
    if (b->res) b->res->Release();
    free(b->cpu_copy);
    free(b);
}

size_t wb_gpu_buffer_size(const wb_gpu_buffer *b) { return b ? b->size : 0; }

void *wb_gpu_buffer_map(wb_gpu_buffer *b) {
    if (!b || !b->res) return NULL;
    void *m = NULL;
    D3D12_RANGE r = {0, b->size};
    if (wb_gpu_failed(b->res->Map(0, &r, &m))) return NULL;
    b->mapped = m;
    return m;
}

void wb_gpu_buffer_unmap(wb_gpu_buffer *b) {
    if (!b || !b->res) return;
    b->res->Unmap(0, NULL);
    b->mapped = NULL;
}

/*
 * wb_gpu_internal.h -- private state of the D3D12 backend.
 *
 * Included only by the wb_gpu*.c translation units. The public header keeps
 * every GPU object opaque; this is where the structs and the shared helpers
 * live.
 *
 * Compiled as C++ (MinGW's d3d12.h exposes C++ interfaces); the public header
 * is extern "C", so the rest of the engine stays C11.
 */
#ifndef WB_GPU_INTERNAL_H
#define WB_GPU_INTERNAL_H

#include "wb_gpu.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>

#include <stddef.h>
#include <stdint.h>

#define WB_GPU_RTV_CAP 64
#define WB_GPU_DSV_CAP 16
#define WB_GPU_SRV_CAP 256
#define WB_GPU_MAX_PASSES 64

/* ------------------------------------------------------------- helpers */

void wb_gpu_set_err(wb_gpu *g, const char *msg);
int  wb_gpu_failed(HRESULT hr);
DXGI_FORMAT wb_gpu_dxgi_format(wb_gpu_format f);
int  wb_gpu_is_depth_format(wb_gpu_format f);

typedef enum {
    WB_USE_NONE = 0,
    WB_USE_RENDER_TARGET,
    WB_USE_DEPTH,
    WB_USE_SHADER_RESOURCE
} wb_use;

D3D12_RESOURCE_STATES wb_gpu_state_for(wb_use u);

ID3D12Resource *wb_gpu_make_buffer_resource(wb_gpu *g, size_t size,
                                            D3D12_HEAP_TYPE heap,
                                            D3D12_RESOURCE_STATES state);

/* ------------------------------------------------------------- objects */

typedef struct {
    wb_gpu_texture *tex;
    wb_use          use;
} wb_res_state;

struct wb_gpu_texture {
    ID3D12Resource *res;
    DXGI_FORMAT     fmt;
    int             w, h;
    wb_gpu_format   format;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv;
    D3D12_CPU_DESCRIPTOR_HANDLE srv;
    int             has_rtv, has_dsv, has_srv;
    int             srv_index;   /* index into the CBV/SRV/UAV heap */
    wb_use          state;
    unsigned char  *backing;   /* for readback textures */
};

struct wb_gpu_buffer {
    ID3D12Resource *res;
    size_t          size;
    wb_gpu_buffer_kind kind;
    void           *mapped;
    UINT8          *cpu_copy;  /* readback staging */
};

struct wb_gpu_shader {
    ID3DBlob *blob;
    wb_gpu_shader_stage stage;
};

struct wb_gpu_pipeline {
    ID3D12PipelineState *pso;
};

/* ---------------------------------------------------------- render graph */

typedef struct {
    wb_gpu_pipeline *pipe; wb_gpu_buffer *vb, *ib;
    int vc, ic, inst, first;
} wb_draw_cmd;

typedef struct {
    char           name[64];
    wb_gpu_texture *color[4];
    float          clear[4][4];
    int            clear_color[4];
    int            ncolor;
    wb_gpu_texture *depth;
    int            clear_depth;
    float          depth_val;
    wb_gpu_texture *reads[8];
    int            nreads;

    wb_draw_cmd draws[256];
    int ndraws;
} wb_pass_impl;

struct wb_gpu_pass {
    wb_pass_impl *impl;
};

/* --------------------------------------------------------------- device */

struct wb_gpu {
    ID3D12Device   *dev;
    ID3D12CommandQueue *queue;
    ID3D12CommandAllocator *alloc;
    ID3D12GraphicsCommandList *list;
    ID3D12Fence    *fence;
    HANDLE          fence_event;
    UINT64          fence_value;

    ID3D12DescriptorHeap *rtv_heap;
    ID3D12DescriptorHeap *dsv_heap;
    ID3D12DescriptorHeap *srv_heap;
    UINT rtv_sz, dsv_sz, srv_sz;
    int  rtv_used, dsv_used, srv_used;

    ID3D12RootSignature *root_sig;

    wb_pass_impl passes[WB_GPU_MAX_PASSES];
    int        npasses;
    int        recording;

    wb_res_state states[256];
    int          nstates;

    IDXGIFactory6  *factory;
    IDXGISwapChain3 *swap;
    int             swap_w, swap_h;
    wb_gpu_texture *swap_rtv[2];
    int             swap_back;

    char err[512];
};

#endif /* WB_GPU_INTERNAL_H */

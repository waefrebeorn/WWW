/*
 * wb_gpu_d3d12.c -- WuBu Engine GPU backend, Direct3D 12 (slice 11).
 * See wb_gpu.h for the contract and the design notes.
 *
 * Takes the render-graph architecture from Redot's RenderingDeviceGraph
 * (thirdparty/redot, reference): passes declare resource usage, the backend
 * derives barriers from usage transitions, and commands are recorded into a
 * command list and submitted as a unit.
 */
#include "wb_gpu.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define WB_GPU_RTV_CAP 64
#define WB_GPU_DSV_CAP 16
#define WB_GPU_SRV_CAP 256
#define WB_GPU_MAX_PASSES 64

/* ------------------------------------------------------------------ */
/* internal types                                                      */
/* ------------------------------------------------------------------ */

typedef enum {
    WB_USE_NONE = 0,
    WB_USE_RENDER_TARGET,
    WB_USE_DEPTH,
    WB_USE_SHADER_RESOURCE
} wb_use;

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

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static void set_err(wb_gpu *g, const char *msg) {
    snprintf(g->err, sizeof g->err, "%s", msg ? msg : "unknown gpu error");
}

static int failed(HRESULT hr) { return (int)hr < 0; }

static DXGI_FORMAT dxgi_format(wb_gpu_format f) {
    switch (f) {
        case WB_GPU_FMT_R8:      return DXGI_FORMAT_R8_UNORM;
        case WB_GPU_FMT_RGBA8:   return DXGI_FORMAT_R8G8B8A8_UNORM;
        case WB_GPU_FMT_RGBA32F: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case WB_GPU_FMT_D24S8:   return DXGI_FORMAT_D24_UNORM_S8_UINT;
    }
    return DXGI_FORMAT_UNKNOWN;
}

static int is_depth_format(wb_gpu_format f) { return f == WB_GPU_FMT_D24S8; }

static D3D12_RESOURCE_STATES state_for(wb_use u) {
    switch (u) {
        case WB_USE_RENDER_TARGET:   return D3D12_RESOURCE_STATE_RENDER_TARGET;
        case WB_USE_DEPTH:           return D3D12_RESOURCE_STATE_DEPTH_WRITE;
        case WB_USE_SHADER_RESOURCE: return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        default:                     return D3D12_RESOURCE_STATE_COMMON;
    }
}

/* ------------------------------------------------------------------ */
/* device                                                              */
/* ------------------------------------------------------------------ */

static int make_root_signature(wb_gpu *g) {
    /* One root signature for every pipeline:
     *   b0 : CBV  per-object constants (float4x4 mvp + float4 albedo)
     *   t0..t3 : SRV descriptor table (textures)
     *   s0 : static sampler
     */
    D3D12_DESCRIPTOR_RANGE ranges[1];
    memset(ranges, 0, sizeof ranges);
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 4;
    ranges[0].BaseShaderRegister = 0;
    ranges[0].RegisterSpace = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER params[2];
    memset(params, 0, sizeof params);
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].Descriptor.RegisterSpace = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = ranges;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samp;
    memset(&samp, 0, sizeof samp);
    samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.ShaderRegister = 0;
    samp.RegisterSpace = 0;
    samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC desc;
    memset(&desc, 0, sizeof desc);
    desc.NumParameters = 2;
    desc.pParameters = params;
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers = &samp;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ID3DBlob *sig = NULL, *errb = NULL;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &errb);
    if (failed(hr)) {
        if (errb) { set_err(g, (const char *)errb->GetBufferPointer()); errb->Release(); }
        else set_err(g, "root signature serialize failed");
        if (sig) sig->Release();
        return -1;
    }
    hr = g->dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                     IID_PPV_ARGS(&g->root_sig));
    sig->Release();
    if (failed(hr)) { set_err(g, "CreateRootSignature failed"); return -1; }
    return 0;
}

wb_gpu *wb_gpu_create(const wb_gpu_desc *desc) {
    wb_gpu *g = (wb_gpu *)calloc(1, sizeof *g);
    if (!g) return NULL;
    int warp = desc ? desc->use_warp : 0;

    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&g->factory));
    if (failed(hr)) { set_err(g, "CreateDXGIFactory1 failed"); free(g); return NULL; }

    /* pick an adapter: hardware preferred, WARP if asked or nothing found */
    IDXGIAdapter1 *adapter = NULL;
    if (!warp) {
        for (UINT i = 0; SUCCEEDED(g->factory->EnumAdapters1(i, &adapter)); ++i) {
            DXGI_ADAPTER_DESC1 d;
            adapter->GetDesc1(&d);
            adapter->Release();
            if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
            /* re-fetch: EnumAdapters1 hands out a new pointer each time */
            if (SUCCEEDED(g->factory->EnumAdapters1(i, &adapter))) break;
            adapter = NULL;
        }
    }
    if (!adapter && !warp) {
        set_err(g, "no hardware adapter found (try use_warp=1)");
        g->factory->Release();
        free(g);
        return NULL;
    }

    hr = D3D12CreateDevice((IUnknown *)adapter, D3D_FEATURE_LEVEL_11_0,
                           IID_PPV_ARGS(&g->dev));
    if (adapter) adapter->Release();
    if (failed(hr)) {
        /* WARP fallback -- D3D12CreateDevice(nullptr) picks WARP */
        hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g->dev));
        if (failed(hr)) { set_err(g, "D3D12CreateDevice failed"); g->factory->Release(); free(g); return NULL; }
    }

    /* command queue */
    D3D12_COMMAND_QUEUE_DESC qd;
    memset(&qd, 0, sizeof qd);
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qd.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    hr = g->dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&g->queue));
    if (failed(hr)) { set_err(g, "CreateCommandQueue failed"); goto fail; }

    hr = g->dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g->alloc));
    if (failed(hr)) { set_err(g, "CreateCommandAllocator failed"); goto fail; }

    hr = g->dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g->alloc, NULL,
                                   IID_PPV_ARGS(&g->list));
    if (failed(hr)) { set_err(g, "CreateCommandList failed"); goto fail; }
    g->list->Close();

    /* fence */
    hr = g->dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g->fence));
    if (failed(hr)) { set_err(g, "CreateFence failed"); goto fail; }
    g->fence_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!g->fence_event) { set_err(g, "CreateEvent failed"); goto fail; }
    g->fence_value = 0;

    /* descriptor heaps */
    D3D12_DESCRIPTOR_HEAP_DESC hd;
    memset(&hd, 0, sizeof hd);
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = WB_GPU_RTV_CAP;
    hr = g->dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&g->rtv_heap));
    if (failed(hr)) { set_err(g, "RTV heap failed"); goto fail; }

    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    hd.NumDescriptors = WB_GPU_DSV_CAP;
    hr = g->dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&g->dsv_heap));
    if (failed(hr)) { set_err(g, "DSV heap failed"); goto fail; }

    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = WB_GPU_SRV_CAP;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = g->dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&g->srv_heap));
    if (failed(hr)) { set_err(g, "SRV heap failed"); goto fail; }

    g->rtv_sz = g->dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    g->dsv_sz = g->dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    g->srv_sz = g->dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    if (make_root_signature(g) != 0) goto fail;

    return g;

fail:
    wb_gpu_destroy(g);
    return NULL;
}

void wb_gpu_destroy(wb_gpu *g) {
    if (!g) return;
    if (g->fence_event) CloseHandle(g->fence_event);
    #define REL(x) do { if (x) { x->Release(); x = NULL; } } while (0)
    REL(g->swap);
    REL(g->root_sig);
    REL(g->srv_heap); REL(g->dsv_heap); REL(g->rtv_heap);
    REL(g->fence); REL(g->list); REL(g->alloc); REL(g->queue);
    REL(g->dev); REL(g->factory);
    #undef REL
    if (g->swap_rtv[0]) wb_gpu_texture_destroy(g->swap_rtv[0]);
    if (g->swap_rtv[1]) wb_gpu_texture_destroy(g->swap_rtv[1]);
    free(g);
}

const char *wb_gpu_error(const wb_gpu *g) { return (g && g->err[0]) ? g->err : NULL; }
int  wb_gpu_ok(const wb_gpu *g) { return g && g->err[0] == 0; }
const char *wb_gpu_backend_name(const wb_gpu *g) { (void)g; return "d3d12"; }

void wb_gpu_get_device_name(const wb_gpu *g, char *out, size_t out_sz) {
    if (!g || !g->dev) { snprintf(out, out_sz, "unknown"); return; }
    LUID luid = g->dev->GetAdapterLuid();
    IDXGIAdapter4 *a = NULL;
    if (SUCCEEDED(g->factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&a)))) {
        DXGI_ADAPTER_DESC3 d;
        a->GetDesc3(&d);
        char tmp[128];
        int n = WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, tmp, sizeof tmp, NULL, NULL);
        a->Release();
        if (n > 0) { snprintf(out, out_sz, "%s", tmp); return; }
    }
    snprintf(out, out_sz, "d3d12 adapter");
}

/* ------------------------------------------------------------------ */
/* buffers                                                             */
/* ------------------------------------------------------------------ */

static ID3D12Resource *make_buffer_resource(wb_gpu *g, size_t size,
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
        b->res = make_buffer_resource(g, size, D3D12_HEAP_TYPE_READBACK,
                                      D3D12_RESOURCE_STATE_COPY_DEST);
        if (!b->res) { set_err(g, "readback buffer failed"); free(b); return NULL; }
        b->cpu_copy = (UINT8 *)calloc(1, size);
        return b;
    }

    b->res = make_buffer_resource(g, size, D3D12_HEAP_TYPE_DEFAULT,
                                  D3D12_RESOURCE_STATE_COPY_DEST);
    if (!b->res) { set_err(g, "buffer resource failed"); free(b); return NULL; }

    if (initial) {
        ID3D12Resource *up = make_buffer_resource(g, size, D3D12_HEAP_TYPE_UPLOAD,
                                                 D3D12_RESOURCE_STATE_GENERIC_READ);
        if (!up) { set_err(g, "upload buffer failed"); wb_gpu_buffer_destroy(b); return NULL; }
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
    if (failed(b->res->Map(0, &r, &m))) return NULL;
    b->mapped = m;
    return m;
}

void wb_gpu_buffer_unmap(wb_gpu_buffer *b) {
    if (!b || !b->res) return;
    b->res->Unmap(0, NULL);
    b->mapped = NULL;
}

/* ------------------------------------------------------------------ */
/* textures                                                            */
/* ------------------------------------------------------------------ */

wb_gpu_texture *wb_gpu_texture_create(wb_gpu *g, wb_gpu_format fmt,
                                      int width, int height, const void *initial) {
    if (!g || width <= 0 || height <= 0) return NULL;
    wb_gpu_texture *t = (wb_gpu_texture *)calloc(1, sizeof *t);
    if (!t) return NULL;
    t->w = width; t->h = height; t->format = fmt; t->fmt = dxgi_format(fmt);
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
    if (is_depth_format(fmt)) { cv.DepthStencil.Depth = 1.0f; cv.DepthStencil.Stencil = 0; pcv = &cv; }
    else { cv.Color[0]=cv.Color[1]=cv.Color[2]=cv.Color[3]=0.0f; pcv = &cv; }

    D3D12_RESOURCE_STATES st = is_depth_format(fmt) ? D3D12_RESOURCE_STATE_DEPTH_WRITE
                                                   : D3D12_RESOURCE_STATE_COPY_DEST;
    rd.Flags = is_depth_format(fmt) ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
                                   : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    HRESULT hr = g->dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, st, pcv,
                                                 IID_PPV_ARGS(&t->res));
    if (failed(hr)) { set_err(g, "texture resource failed"); free(t); return NULL; }
    t->state = is_depth_format(fmt) ? WB_USE_DEPTH : WB_USE_RENDER_TARGET;

    /* descriptors */
    if (!is_depth_format(fmt)) {
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
        ID3D12Resource *up = make_buffer_resource(g, bytes, D3D12_HEAP_TYPE_UPLOAD,
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

/* ------------------------------------------------------------------ */
/* shaders + pipelines                                                 */
/* ------------------------------------------------------------------ */

wb_gpu_shader *wb_gpu_shader_create(wb_gpu *g, wb_gpu_shader_stage stage,
                                    const char *hlsl, const char *entry) {
    if (!g || !hlsl || !entry) return NULL;
    ID3DBlob *blob = NULL, *errb = NULL;
    UINT flags = 0;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    HRESULT hr = D3DCompile(hlsl, strlen(hlsl), NULL, NULL, NULL, entry,
                            stage == WB_GPU_SHADER_VERTEX ? "vs_5_0" : "ps_5_0",
                            flags, 0, &blob, &errb);
    if (failed(hr)) {
        if (errb) { set_err(g, (const char *)errb->GetBufferPointer()); errb->Release(); }
        else set_err(g, "D3DCompile failed");
        if (blob) blob->Release();
        return NULL;
    }
    if (errb) errb->Release();
    wb_gpu_shader *s = (wb_gpu_shader *)calloc(1, sizeof *s);
    s->blob = blob; s->stage = stage;
    return s;
}

void wb_gpu_shader_destroy(wb_gpu_shader *s) {
    if (!s) return;
    if (s->blob) s->blob->Release();
    free(s);
}

wb_gpu_pipeline *wb_gpu_pipeline_create(wb_gpu *g, wb_gpu_shader *vs, wb_gpu_shader *ps) {
    if (!g || !vs || !vs->blob) return NULL;
    wb_gpu_pipeline *p = (wb_gpu_pipeline *)calloc(1, sizeof *p);

    D3D12_INPUT_ELEMENT_DESC il[2];
    memset(il, 0, sizeof il);
    il[0].SemanticName = "POSITION"; il[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    il[0].AlignedByteOffset = 0; il[0].InputSlot = 0;
    il[0].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    il[1].SemanticName = "NORMAL"; il[1].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    il[1].AlignedByteOffset = 12; il[1].InputSlot = 0;
    il[1].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd;
    memset(&pd, 0, sizeof pd);
    pd.pRootSignature = g->root_sig;
    pd.VS.pShaderBytecode = vs->blob->GetBufferPointer();
    pd.VS.BytecodeLength  = vs->blob->GetBufferSize();
    if (ps && ps->blob) {
        pd.PS.pShaderBytecode = ps->blob->GetBufferPointer();
        pd.PS.BytecodeLength  = ps->blob->GetBufferSize();
    }
    pd.InputLayout.pInputElementDescs = il;
    pd.InputLayout.NumElements = 2;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pd.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    pd.SampleDesc.Count = 1;
    pd.SampleMask = UINT_MAX;   /* 0 would mask out every sample */
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    pd.RasterizerState.FrontCounterClockwise = FALSE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.DepthStencilState.DepthEnable = TRUE;
    pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    HRESULT hr = g->dev->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&p->pso));
    if (failed(hr)) { set_err(g, "CreateGraphicsPipelineState failed"); free(p); return NULL; }
    return p;
}

void wb_gpu_pipeline_destroy(wb_gpu_pipeline *p) {
    if (!p) return;
    if (p->pso) p->pso->Release();
    free(p);
}

/* ------------------------------------------------------------------ */
/* render graph                                                        */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/* execution                                                           */
/* ------------------------------------------------------------------ */

int wb_gpu_execute(wb_gpu *g) {
    if (!g) return -1;
    g->err[0] = 0;

    g->alloc->Reset();
    HRESULT hr = g->list->Reset(g->alloc, NULL);
    if (failed(hr)) { set_err(g, "command list reset failed"); return -1; }

    ID3D12DescriptorHeap *heaps[1] = { g->srv_heap };
    g->list->SetDescriptorHeaps(1, heaps);
    g->list->SetGraphicsRootSignature(g->root_sig);

    /* reset per-frame descriptor allocation for SRV table use */
    /* (textures keep their descriptors; we only bind them per draw) */

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
                bar.Transition.StateBefore = state_for(t->state);
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
                bar.Transition.StateBefore = state_for(t->state);
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
                bar.Transition.StateBefore = state_for(t->state);
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

/* ------------------------------------------------------------------ */
/* readback                                                            */
/* ------------------------------------------------------------------ */

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
    bar.Transition.StateBefore = state_for(tex->state);
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
    bar.Transition.StateAfter = state_for(tex->state);
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
    if (failed(rb->res->Map(0, &r, &m))) { wb_gpu_buffer_destroy(rb); return -1; }
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

/* ------------------------------------------------------------------ */
/* present (windowed)                                                  */
/* ------------------------------------------------------------------ */

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
        if (failed(hr)) { set_err(g, "CreateSwapChainForHwnd failed"); return -1; }
        if (failed(sc1->QueryInterface(IID_PPV_ARGS(&g->swap)))) {
            set_err(g, "swapchain query failed"); sc1->Release(); return -1;
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

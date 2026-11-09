/*
 * wb_gpu_dev.c -- device, command queue, descriptor heaps, root signature and
 * the module error state. See wb_gpu_internal.h.
 */
#include "wb_gpu_internal.h"

#include <stdio.h>
#include <string.h>

void wb_gpu_set_err(wb_gpu *g, const char *msg) {
    snprintf(g->err, sizeof g->err, "%s", msg ? msg : "unknown gpu error");
}

int wb_gpu_failed(HRESULT hr) { return (int)hr < 0; }

DXGI_FORMAT wb_gpu_dxgi_format(wb_gpu_format f) {
    switch (f) {
        case WB_GPU_FMT_R8:      return DXGI_FORMAT_R8_UNORM;
        case WB_GPU_FMT_RGBA8:   return DXGI_FORMAT_R8G8B8A8_UNORM;
        case WB_GPU_FMT_RGBA32F: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case WB_GPU_FMT_D24S8:   return DXGI_FORMAT_D24_UNORM_S8_UINT;
    }
    return DXGI_FORMAT_UNKNOWN;
}

int wb_gpu_is_depth_format(wb_gpu_format f) { return f == WB_GPU_FMT_D24S8; }

D3D12_RESOURCE_STATES wb_gpu_state_for(wb_use u) {
    switch (u) {
        case WB_USE_RENDER_TARGET:   return D3D12_RESOURCE_STATE_RENDER_TARGET;
        case WB_USE_DEPTH:           return D3D12_RESOURCE_STATE_DEPTH_WRITE;
        case WB_USE_SHADER_RESOURCE: return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        default:                     return D3D12_RESOURCE_STATE_COMMON;
    }
}

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
    if (wb_gpu_failed(hr)) {
        if (errb) { wb_gpu_set_err(g, (const char *)errb->GetBufferPointer()); errb->Release(); }
        else wb_gpu_set_err(g, "root signature serialize failed");
        if (sig) sig->Release();
        return -1;
    }
    hr = g->dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                     IID_PPV_ARGS(&g->root_sig));
    sig->Release();
    if (wb_gpu_failed(hr)) { wb_gpu_set_err(g, "CreateRootSignature failed"); return -1; }
    return 0;
}

wb_gpu *wb_gpu_create(const wb_gpu_desc *desc) {
    wb_gpu *g = (wb_gpu *)calloc(1, sizeof *g);
    if (!g) return NULL;
    int warp = desc ? desc->use_warp : 0;

    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&g->factory));
    if (wb_gpu_failed(hr)) { wb_gpu_set_err(g, "CreateDXGIFactory1 failed"); free(g); return NULL; }

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
        wb_gpu_set_err(g, "no hardware adapter found (try use_warp=1)");
        g->factory->Release();
        free(g);
        return NULL;
    }

    hr = D3D12CreateDevice((IUnknown *)adapter, D3D_FEATURE_LEVEL_11_0,
                           IID_PPV_ARGS(&g->dev));
    if (adapter) adapter->Release();
    if (wb_gpu_failed(hr)) {
        /* WARP fallback -- D3D12CreateDevice(nullptr) picks WARP */
        hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g->dev));
        if (wb_gpu_failed(hr)) {
            wb_gpu_set_err(g, "D3D12CreateDevice failed");
            g->factory->Release();
            free(g);
            return NULL;
        }
    }

    /* command queue */
    D3D12_COMMAND_QUEUE_DESC qd;
    memset(&qd, 0, sizeof qd);
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qd.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    hr = g->dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&g->queue));
    if (wb_gpu_failed(hr)) { wb_gpu_set_err(g, "CreateCommandQueue failed"); goto fail; }

    hr = g->dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g->alloc));
    if (wb_gpu_failed(hr)) { wb_gpu_set_err(g, "CreateCommandAllocator failed"); goto fail; }

    hr = g->dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g->alloc, NULL,
                                   IID_PPV_ARGS(&g->list));
    if (wb_gpu_failed(hr)) { wb_gpu_set_err(g, "CreateCommandList failed"); goto fail; }
    g->list->Close();

    /* fence */
    hr = g->dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g->fence));
    if (wb_gpu_failed(hr)) { wb_gpu_set_err(g, "CreateFence failed"); goto fail; }
    g->fence_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!g->fence_event) { wb_gpu_set_err(g, "CreateEvent failed"); goto fail; }
    g->fence_value = 0;

    /* descriptor heaps */
    D3D12_DESCRIPTOR_HEAP_DESC hd;
    memset(&hd, 0, sizeof hd);
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = WB_GPU_RTV_CAP;
    hr = g->dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&g->rtv_heap));
    if (wb_gpu_failed(hr)) { wb_gpu_set_err(g, "RTV heap failed"); goto fail; }

    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    hd.NumDescriptors = WB_GPU_DSV_CAP;
    hr = g->dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&g->dsv_heap));
    if (wb_gpu_failed(hr)) { wb_gpu_set_err(g, "DSV heap failed"); goto fail; }

    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = WB_GPU_SRV_CAP;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = g->dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&g->srv_heap));
    if (wb_gpu_failed(hr)) { wb_gpu_set_err(g, "SRV heap failed"); goto fail; }

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

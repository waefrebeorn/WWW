/*
 * wb_gpu_shader.c -- runtime HLSL compilation and pipeline state. See wb_gpu.h.
 */
#include "wb_gpu_internal.h"

#include <stdlib.h>
#include <string.h>

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
    if (wb_gpu_failed(hr)) {
        if (errb) { wb_gpu_set_err(g, (const char *)errb->GetBufferPointer()); errb->Release(); }
        else wb_gpu_set_err(g, "D3DCompile failed");
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
    if (wb_gpu_failed(hr)) { wb_gpu_set_err(g, "CreateGraphicsPipelineState failed"); free(p); return NULL; }
    return p;
}

void wb_gpu_pipeline_destroy(wb_gpu_pipeline *p) {
    if (!p) return;
    if (p->pso) p->pso->Release();
    free(p);
}

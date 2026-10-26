/* RC44: original synthetic material/instance discriminator. Included after the
 * common probe helpers; explicitly selected by name, never in the smoke run.
 * SM5.1 compiles locally through the system compiler; the default R7 runtime
 * converts it to DXIL. Capture receipts must confirm that route on the bench. */
#ifndef AGX_D3D12_MATERIAL_PROBE_H
#define AGX_D3D12_MATERIAL_PROBE_H
#include <d3dcompiler.h>
#include "agx_d3d12_probe_material_oracle.h"
#ifdef __cplusplus
#define RC44_IID(x) (x)
#else
#define RC44_IID(x) (&(x))
#endif

/* ps_5_1 forbids UAV-derived values in implicit-gradient samples (X3662).
 * These textures have one mip: explicit LOD 0 preserves the u4 material path
 * and the colour oracle without requiring derivatives of the selected layer. */
static const char rc44_hlsl[] =
    "struct V { float4 p:POSITION; float4 n:TEXCOORD0; float2 uv:TEXCOORD1; uint4 data:TEXCOORD2; };\n"
    "struct P { float4 p:SV_Position; float3 n:TEXCOORD0; float4 tangent:TEXCOORD1; float2 uv:TEXCOORD2; nointerpolation uint3 ids:TEXCOORD3; };\n"
    "P vs_main(V v) { P o; o.p=v.p; o.n=v.n.xyz; o.tangent=float4(1,0,0,1); o.uv=v.uv; o.ids=v.data.xyz; return o; }\n"
    "Texture2DArray<float4> materials[8]:register(t3); SamplerState samp:register(s3);\n"
    "RWByteAddressBuffer records:register(u4);\n"
    "struct O { float4 colour:SV_Target0; float4 index:SV_Target1; };\n"
    "O ps_main(P i) { uint word=records.Load(i.ids.x*16+4); uint d=min(word>>16,7u); uint layer=word&65535u;\n"
    "O o; o.colour=materials[d].SampleLevel(samp,float3(i.uv,layer),0)*float4(i.n.zzz,1);\n"
    "o.index=float4(i.ids.y,d,layer,255)/255.0; return o; }\n";

static int rc44_material(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base, int indirect) {
    const char *step = indirect ? "RC44I" : "RC44D";
    HMODULE compiler = LoadLibraryExW(L"d3dcompiler_47.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    pD3DCompile compile = compiler ? (pD3DCompile)(void *)GetProcAddress(compiler, "D3DCompile") : NULL;
    ID3DBlob *vs = NULL, *ps = NULL, *blob = NULL, *errors = NULL;
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL;
    ID3D12CommandSignature *signature = NULL;
    ID3D12DescriptorHeap *srv_heap = NULL, *rtv_heap = NULL;
    ID3D12Resource *texture[8] = {0}, *target[2] = {0};
    ID3D12Resource *upload = NULL, *readback = NULL, *material = NULL;
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv[2] = {{0}};
    D3D12_GPU_DESCRIPTOR_HANDLE srv = {0};
    D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC td = {D3D12_RESOURCE_DIMENSION_UNKNOWN};
    D3D12_DESCRIPTOR_HEAP_DESC sh = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    D3D12_DESCRIPTOR_HEAP_DESC rh = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
    D3D12_DESCRIPTOR_RANGE range = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 3, 0, 0};
    D3D12_ROOT_PARAMETER params[2] = {{D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE}, {D3D12_ROOT_PARAMETER_TYPE_UAV}};
    D3D12_STATIC_SAMPLER_DESC sampler = {D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 0, 1, D3D12_COMPARISON_FUNC_NEVER,
        D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK, 0, D3D12_FLOAT32_MAX, 3, 0, D3D12_SHADER_VISIBILITY_PIXEL};
    D3D12_ROOT_SIGNATURE_DESC rd = {2, params, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    D3D12_INPUT_ELEMENT_DESC elements[4] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,16,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TEXCOORD",1,DXGI_FORMAT_R32G32_FLOAT,0,32,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TEXCOORD",2,DXGI_FORMAT_R32G32B32A32_UINT,1,0,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1}
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {0};
    HRESULT hr = compile ? S_OK : E_FAIL;
    unsigned bad = 32;
    if (SUCCEEDED(hr)) hr = compile(rc44_hlsl, sizeof(rc44_hlsl)-1, "rc44-original", NULL, NULL, "vs_main", "vs_5_1",
                                    D3DCOMPILE_ENABLE_STRICTNESS, 0, &vs, &errors);
    if (errors) { printf("%s VS: %s\n", step, (const char *)ID3D10Blob_GetBufferPointer(errors)); ID3D10Blob_Release(errors); errors = NULL; }
    if (SUCCEEDED(hr)) hr = compile(rc44_hlsl, sizeof(rc44_hlsl)-1, "rc44-original", NULL, NULL, "ps_main", "ps_5_1",
                                    D3DCOMPILE_ENABLE_STRICTNESS, 0, &ps, &errors);
    if (errors) { printf("%s PS: %s\n", step, (const char *)ID3D10Blob_GetBufferPointer(errors)); ID3D10Blob_Release(errors); errors = NULL; }
    if (SUCCEEDED(hr)) {
        upload = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        material = uav_buffer(device);
        if (!upload || !readback || !material) hr = E_FAIL;
    }
    if (SUCCEEDED(hr)) {
        unsigned char *bytes = NULL;
        hr = ID3D12Resource_Map(upload, 0, NULL, (void **)&bytes);
        if (SUCCEEDED(hr) && !bytes) hr = E_FAIL;
        if (SUCCEEDED(hr)) {
            const float vertices[3][10] = {{-1,-1,0,1, 0,0,1,0, .5f,.5f}, {-1,3,0,1, 0,0,1,0, .5f,.5f}, {3,-1,0,1, 0,0,1,0, .5f,.5f}};
            const UINT indices[3] = {0,1,2};
            const UINT instances[6][4] = {{0,0,0,0},{0,1,0,0},{0,2,0,0},{2,3,0,0},{0,4,0,0},{4,5,0,0}};
            const UINT one = 1, packed[2] = {(2u << 16) | 1u, 7u << 16};
            memset(bytes, 0, 16384);
            memcpy(bytes, vertices, sizeof(vertices)); memcpy(bytes+128, indices, sizeof(indices));
            memcpy(bytes+256, instances, sizeof(instances)); memcpy(bytes+768, &one, sizeof(one));
            for (UINT c = 0; c < 2; ++c) {
                const D3D12_DRAW_INDEXED_ARGUMENTS args[2] = {{3,1,0,0,c ? 5u : 3u}, {3,1,0,0,0}};
                memcpy(bytes+512+c*64, args, sizeof(args)); /* poison second draw must be count-suppressed */
                memcpy(bytes+1024+(c ? 4 : 2)*16+4, &packed[c], sizeof(UINT));
            }
            for (UINT d = 0; d < 8; ++d) for (UINT layer = 0; layer < 2; ++layer) {
                const unsigned char texel[4] = {(unsigned char)(17+19*d+7*layer), (unsigned char)(29+11*d+13*layer),
                                                (unsigned char)(43+5*d+17*layer),255};
                memcpy(bytes+2048+d*1024+layer*512, texel, 4);
            }
            ID3D12Resource_Unmap(upload, 0, NULL);
        }
    }
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; td.Width = td.Height = 1;
    td.DepthOrArraySize = 2; td.MipLevels = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    for (UINT i = 0; SUCCEEDED(hr) && i < 8; ++i)
        hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST,
                                                 NULL, RC44_IID(IID_ID3D12Resource), (void **)&texture[i]);
    td.Width = td.Height = 4; td.DepthOrArraySize = 1; td.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    for (UINT i = 0; SUCCEEDED(hr) && i < 2; ++i)
        hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                 NULL, RC44_IID(IID_ID3D12Resource), (void **)&target[i]);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &sh, RC44_IID(IID_ID3D12DescriptorHeap), (void **)&srv_heap);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &rh, RC44_IID(IID_ID3D12DescriptorHeap), (void **)&rtv_heap);
    if (SUCCEEDED(hr)) {
        D3D12_CPU_DESCRIPTOR_HANDLE at;
        UINT inc = ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_SHADER_RESOURCE_VIEW_DESC view = {DXGI_FORMAT_UNKNOWN};
        view.Format = DXGI_FORMAT_R8G8B8A8_UNORM; view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture2DArray.MipLevels = 1; view.Texture2DArray.ArraySize = 2;
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(srv_heap, &at);
        ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(srv_heap, &srv);
        for (UINT i = 0; i < 8; ++i, at.ptr += inc) ID3D12Device_CreateShaderResourceView(device, texture[i], &view, at);
        inc = ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap, &at);
        for (UINT i = 0; i < 2; ++i, at.ptr += inc) { rtv[i] = at; ID3D12Device_CreateRenderTargetView(device, target[i], NULL, at); }
        params[0].DescriptorTable.NumDescriptorRanges = 1; params[0].DescriptorTable.pDescriptorRanges = &range;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[1].Descriptor.ShaderRegister = 4; params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        hr = D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    }
    if (errors) { printf("%s root: %s\n", step, (const char *)ID3D10Blob_GetBufferPointer(errors)); ID3D10Blob_Release(errors); errors = NULL; }
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
        ID3D10Blob_GetBufferSize(blob), RC44_IID(IID_ID3D12RootSignature), (void **)&root);
    if (SUCCEEDED(hr)) {
        pd.pRootSignature = root;
        pd.VS.pShaderBytecode = ID3D10Blob_GetBufferPointer(vs); pd.VS.BytecodeLength = ID3D10Blob_GetBufferSize(vs);
        pd.PS.pShaderBytecode = ID3D10Blob_GetBufferPointer(ps); pd.PS.BytecodeLength = ID3D10Blob_GetBufferSize(ps);
        pd.SampleMask = ~0u; pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; pd.RasterizerState.DepthClipEnable = TRUE;
        pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pd.DepthStencilState.FrontFace.StencilFailOp = pd.DepthStencilState.FrontFace.StencilDepthFailOp =
            pd.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
        pd.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pd.DepthStencilState.BackFace = pd.DepthStencilState.FrontFace;
        pd.InputLayout.pInputElementDescs = elements; pd.InputLayout.NumElements = 4;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; pd.NumRenderTargets = 2; pd.SampleDesc.Count = 1;
        for (UINT i = 0; i < 2; ++i) {
            pd.RTVFormats[i] = DXGI_FORMAT_R8G8B8A8_UNORM;
            pd.BlendState.RenderTarget[i].SrcBlend = pd.BlendState.RenderTarget[i].SrcBlendAlpha = D3D12_BLEND_ONE;
            pd.BlendState.RenderTarget[i].DestBlend = pd.BlendState.RenderTarget[i].DestBlendAlpha = D3D12_BLEND_ZERO;
            pd.BlendState.RenderTarget[i].BlendOp = pd.BlendState.RenderTarget[i].BlendOpAlpha = D3D12_BLEND_OP_ADD;
            pd.BlendState.RenderTarget[i].LogicOp = D3D12_LOGIC_OP_NOOP;
            pd.BlendState.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        }
        hr = ID3D12Device_CreateGraphicsPipelineState(device, &pd, RC44_IID(IID_ID3D12PipelineState), (void **)&pso);
    }
    if (SUCCEEDED(hr) && indirect) {
        D3D12_INDIRECT_ARGUMENT_DESC arg = {D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED};
        D3D12_COMMAND_SIGNATURE_DESC sd = {sizeof(D3D12_DRAW_INDEXED_ARGUMENTS),1,&arg,0};
        hr = ID3D12Device_CreateCommandSignature(device, &sd, NULL, RC44_IID(IID_ID3D12CommandSignature), (void **)&signature);
    }
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT, RC44_IID(IID_ID3D12CommandAllocator), (void **)&allocator);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pso, RC44_IID(IID_ID3D12GraphicsCommandList), (void **)&list);
    printf("%s setup hr=0x%08lx\n", step, (unsigned long)hr);
    if (SUCCEEDED(hr)) {
        D3D12_TEXTURE_COPY_LOCATION src = {0}, dst = {0};
        D3D12_VERTEX_BUFFER_VIEW vb[2] = {{ID3D12Resource_GetGPUVirtualAddress(upload),120,40},
                                         {ID3D12Resource_GetGPUVirtualAddress(upload)+256,96,16}};
        D3D12_INDEX_BUFFER_VIEW ib = {ID3D12Resource_GetGPUVirtualAddress(upload)+128,12,DXGI_FORMAT_R32_UINT};
        D3D12_VIEWPORT viewport = {0,0,4,4,0,1}; D3D12_RECT scissor = {0,0,4,4};
        const FLOAT clear[4] = {1,0,1,0};
        src.pResource = upload; src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        src.PlacedFootprint.Footprint.Width = src.PlacedFootprint.Footprint.Height = src.PlacedFootprint.Footprint.Depth = 1;
        src.PlacedFootprint.Footprint.RowPitch = 256; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        for (UINT d = 0; d < 8; ++d) {
            dst.pResource = texture[d];
            for (UINT layer = 0; layer < 2; ++layer) {
                dst.SubresourceIndex = layer; src.PlacedFootprint.Offset = 2048+d*1024+layer*512;
                ID3D12GraphicsCommandList_CopyTextureRegion(list, &dst, 0,0,0, &src, NULL);
            }
            transition(list, texture[d], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
        transition(list, material, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
        ID3D12GraphicsCommandList_CopyBufferRegion(list, material, 0, upload, 1024, 96);
        transition(list, material, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 1, &srv_heap);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, root);
        ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(list, 0, srv);
        ID3D12GraphicsCommandList_SetGraphicsRootUnorderedAccessView(list, 1, ID3D12Resource_GetGPUVirtualAddress(material));
        ID3D12GraphicsCommandList_RSSetViewports(list, 1, &viewport); ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &scissor);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_IASetVertexBuffers(list, 0, 2, vb); ID3D12GraphicsCommandList_IASetIndexBuffer(list, &ib);
        for (UINT c = 0; c < 2; ++c) {
            for (UINT t = 0; t < 2; ++t) ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtv[t], clear, 0, NULL);
            ID3D12GraphicsCommandList_OMSetRenderTargets(list, 2, rtv, FALSE, NULL);
            if (indirect) ID3D12GraphicsCommandList_ExecuteIndirect(list, signature, 2, upload, 512+c*64, upload, 768);
            else ID3D12GraphicsCommandList_DrawIndexedInstanced(list, 3,1,0,0, c ? 5 : 3);
            for (UINT t = 0; t < 2; ++t) {
                D3D12_TEXTURE_COPY_LOCATION from = {0}, to = {0};
                transition(list, target[t], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
                from.pResource = target[t]; from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                to.pResource = readback; to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                to.PlacedFootprint.Offset = c*2048+t*1024; to.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                to.PlacedFootprint.Footprint.Width = to.PlacedFootprint.Footprint.Height = 4;
                to.PlacedFootprint.Footprint.Depth = 1; to.PlacedFootprint.Footprint.RowPitch = 256;
                ID3D12GraphicsCommandList_CopyTextureRegion(list, &to, 0,0,0, &from, NULL);
                transition(list, target[t], D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
            }
        }
        hr = run_list(queue, fence, list, base+1, step);
    }
    if (SUCCEEDED(hr)) {
        unsigned char *bytes = NULL; D3D12_RANGE read = {0,4096};
        hr = ID3D12Resource_Map(readback, 0, &read, (void **)&bytes);
        if (SUCCEEDED(hr) && !bytes) hr = E_FAIL;
        if (SUCCEEDED(hr)) {
            bad = 0;
            for (UINT c = 0; c < 2; ++c) {
                const unsigned char *colour = bytes+c*2048, *index = colour+1024;
                unsigned n = rc44_bad_pixels(colour, index, c); bad += n;
                printf("%s case=%u colour=%u,%u,%u,%u index=%u,%u,%u,%u pixels=16 bad=%u\n", step,c,
                    colour[0],colour[1],colour[2],colour[3],index[0],index[1],index[2],index[3],n);
            }
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    printf("AGX_D3D12_%s: %s hr=0x%08lx bad=%u\n", step, SUCCEEDED(hr) && !bad ? "PASS" : "FAIL", (unsigned long)hr, bad);
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (signature) ID3D12CommandSignature_Release(signature);
    if (pso) ID3D12PipelineState_Release(pso);
    if (root) ID3D12RootSignature_Release(root);
    if (blob) ID3D10Blob_Release(blob);
    if (vs) ID3D10Blob_Release(vs);
    if (ps) ID3D10Blob_Release(ps);
    if (srv_heap) ID3D12DescriptorHeap_Release(srv_heap);
    if (rtv_heap) ID3D12DescriptorHeap_Release(rtv_heap);
    for (UINT i = 0; i < 8; ++i) if (texture[i]) ID3D12Resource_Release(texture[i]);
    for (UINT i = 0; i < 2; ++i) if (target[i]) ID3D12Resource_Release(target[i]);
    if (material) ID3D12Resource_Release(material);
    if (upload) ID3D12Resource_Release(upload);
    if (readback) ID3D12Resource_Release(readback);
    if (compiler) FreeLibrary(compiler);
    return !(SUCCEEDED(hr) && !bad);
}
static int step_rc44d(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return rc44_material(d,q,f,b,0); }
static int step_rc44i(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return rc44_material(d,q,f,b,1); }
#undef RC44_IID
#endif

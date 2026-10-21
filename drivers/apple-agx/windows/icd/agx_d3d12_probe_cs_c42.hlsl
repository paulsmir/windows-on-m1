// D3D12 C42 probes (EXP1841 MiniEngine yellow scene): fxc /T cs_5_0 /O3
// C42C /E cs_load /Vn g_agx_d3d12_probe_cs_c42c -> agx_d3d12_probe_cs_c42c.h:
//   ld of an R11G11B10_FLOAT texture into a raw buffer
// C42D /E cs_pack /Vn g_agx_d3d12_probe_cs_c42d -> agx_d3d12_probe_cs_c42d.h:
//   MiniEngine's Pack/Unpack_R11G11B10_FLOAT (PixelPacking_R11G11B10.hlsli)
RWByteAddressBuffer o : register(u0);
Texture2D<float3> scene : register(t0);
cbuffer c : register(b0) { float3 rgb; };

[numthreads(1, 1, 1)]
void cs_load()
{
    o.Store3(0, asuint(scene.Load(int3(8, 8, 0))));
}

uint Pack_R11G11B10_FLOAT(float3 v)
{
    v = min(v, asfloat(0x477C0000));
    uint r = ((f32tof16(v.x) + 8) >> 4) & 0x000007FF;
    uint g = ((f32tof16(v.y) + 8) << 7) & 0x003FF800;
    uint b = ((f32tof16(v.z) + 16) << 17) & 0xFFC00000;
    return r | g | b;
}

float3 Unpack_R11G11B10_FLOAT(uint p)
{
    float r = f16tof32((p << 4) & 0x7FF0);
    float g = f16tof32((p >> 7) & 0x7FF0);
    float b = f16tof32((p >> 17) & 0x7FE0);
    return float3(r, g, b);
}

[numthreads(1, 1, 1)]
void cs_pack()
{
    uint p = Pack_R11G11B10_FLOAT(rgb);
    o.Store4(0, uint4(p, asuint(Unpack_R11G11B10_FLOAT(p))));
}

// C42E /E cs_copyback /Vn g_agx_d3d12_probe_cs_c42e -> agx_d3d12_probe_cs_c42e.h:
//   MiniEngine CopyBackPostBufferCS: an R32_UINT packed post buffer unpacked
//   into an R11G11B10_FLOAT typed UAV (float3 store)
RWTexture2D<float3> scene_out : register(u1);
Texture2D<uint> post : register(t1);

[numthreads(8, 8, 1)]
void cs_copyback(uint3 id : SV_DispatchThreadID)
{
    scene_out[id.xy] = Unpack_R11G11B10_FLOAT(post[id.xy]);
}

// C42J /E cs_gather /Vn g_agx_d3d12_probe_cs_c42j -> agx_d3d12_probe_cs_c42j.h:
//   MiniEngine TemporalBlendCS gathers the R11G11B10 scene colour channel by
//   channel (gather4 ... s0.x / .y / .z)
SamplerState gather_point : register(s0);

[numthreads(1, 1, 1)]
void cs_gather()
{
    const float2 uv = float2(0.5, 0.5);
    float4 r = scene.GatherRed(gather_point, uv);
    float4 g = scene.GatherGreen(gather_point, uv);
    float4 b = scene.GatherBlue(gather_point, uv);
    o.Store4(0, asuint(float4(r.x, g.x, b.x, r.w)));
    o.Store4(16, asuint(float4(r.y, g.y, b.y, b.w)));
}

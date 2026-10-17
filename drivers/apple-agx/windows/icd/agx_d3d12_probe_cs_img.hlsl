// D3D12 C9 probe: compute texture SRV t0 + static point sampler s0,
// RWTexture2D u0 (RGBA8), typed RWBuffer<uint> u1 (R32_UINT).
// fxc /T cs_5_0 /E main /O3 /Vn g_agx_d3d12_probe_cs_img -> agx_d3d12_probe_cs_img.h
Texture2D<float4> src : register(t0);
SamplerState point_clamp : register(s0);
RWTexture2D<float4> dst : register(u0);
RWBuffer<uint> typed : register(u1);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    float2 uv = (float2(id.xy) + 0.5) / 64.0;
    dst[id.xy] = src.SampleLevel(point_clamp, uv, 0);
    typed[id.y * 64 + id.x] = id.x + id.y * 1000;
}

// D3D12 C51A probe: agx_d3d12_probe_cs_cb.hlsl (C5b) as SM 6.2 DXIL with fp32 denormals preserved (AGX runs it FTZ; no denormal values occur).
// dxc -T cs_6_2 -E main -denorm preserve -Vn g_agx_d3d12_probe_cs_cb_dxil62 -Fh agx_d3d12_probe_cs_cb_dxil62.h
cbuffer View : register(b0) { uint4 extra; };
cbuffer Root : register(b1) { uint scale; uint bias; };
RWByteAddressBuffer output : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    output.Store(id.x * 4, id.x * scale + bias + extra.y);
}

// D3D12 C44A probe: agx_d3d12_probe_cs_cb.hlsl (C5b) as SM 6.0 DXIL.
// dxc -T cs_6_0 -E main -Vn g_agx_d3d12_probe_cs_cb_dxil -Fh agx_d3d12_probe_cs_cb_dxil.h
cbuffer View : register(b0) { uint4 extra; };
cbuffer Root : register(b1) { uint scale; uint bias; };
RWByteAddressBuffer output : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    output.Store(id.x * 4, id.x * scale + bias + extra.y);
}

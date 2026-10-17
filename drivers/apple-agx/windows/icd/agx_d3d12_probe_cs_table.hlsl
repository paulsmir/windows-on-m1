// D3D12 C5c probe: one descriptor table {UAV u0, CBV b0}.
// fxc /T cs_5_0 /E main /O3 /Vn g_agx_d3d12_probe_cs_table -> agx_d3d12_probe_cs_table.h
cbuffer View : register(b0) { uint4 k; };
RWByteAddressBuffer output : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    output.Store(id.x * 4, id.x * k.x + k.y);
}

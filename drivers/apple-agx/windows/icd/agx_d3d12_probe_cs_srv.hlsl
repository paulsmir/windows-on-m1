// D3D12 C5d probe: root UAV u0, root SRV t0 (raw), table {SRV t1 (structured uint)}.
// fxc /T cs_5_0 /E main /O3 /Vn g_agx_d3d12_probe_cs_srv -> agx_d3d12_probe_cs_srv.h
ByteAddressBuffer a : register(t0);
StructuredBuffer<uint> b : register(t1);
RWByteAddressBuffer output : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    output.Store(id.x * 4, a.Load(id.x * 4) + b[id.x] * 2);
}

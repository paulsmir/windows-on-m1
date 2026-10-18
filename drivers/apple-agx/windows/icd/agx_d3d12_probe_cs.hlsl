// D3D12 C5 probe: one root UAV (u0, raw), 64 threads per group.
// fxc /T cs_5_0 /E main /O3 -> agx_d3d12_probe_cs.h (g_agx_d3d12_probe_cs)
RWByteAddressBuffer output : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    output.Store(id.x * 4, id.x * 3 + 7);
}

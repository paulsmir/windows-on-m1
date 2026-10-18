// D3D12 C5b probe: root UAV u0, root constants b1 {scale, bias}, root CBV b0.
// fxc /T cs_5_0 /E main /O3 /Vn g_agx_d3d12_probe_cs_cb -> agx_d3d12_probe_cs_cb.h
cbuffer View : register(b0) { uint4 extra; };
cbuffer Root : register(b1) { uint scale; uint bias; };
RWByteAddressBuffer output : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    output.Store(id.x * 4, id.x * scale + bias + extra.y);
}

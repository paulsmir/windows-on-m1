// D3D12 C28A probe: groupshared tiles with a barrier inside a loop whose bound
// comes from a root CBV (the nBodyGravityCS structure, minimal body).
// fxc /T cs_5_0 /E main /Vn g_agx_d3d12_probe_cs_tgsm_loop -> agx_d3d12_probe_cs_tgsm_loop.h
cbuffer Tiles : register(b0) { uint4 param; };
RWStructuredBuffer<uint> result : register(u0);
groupshared uint tile_values[64];

[numthreads(64, 1, 1)]
void main(uint3 tid : SV_GroupThreadID, uint3 gid : SV_GroupID)
{
    uint sum = 0;
    for (uint t = 0; t < param.y; ++t) {
        tile_values[tid.x] = t * 64 + tid.x;
        GroupMemoryBarrierWithGroupSync();
        for (uint i = 0; i < 64; ++i) sum += tile_values[i];
        GroupMemoryBarrierWithGroupSync();
    }
    result[gid.x * 64 + tid.x] = sum;
}

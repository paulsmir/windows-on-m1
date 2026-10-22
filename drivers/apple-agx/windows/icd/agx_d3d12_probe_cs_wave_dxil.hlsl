// D3D12 C48A probe: SM 6.0 wave intrinsics in 64-thread groups (two full
// 32-lane waves); each thread stores a mask of checks that hold for any
// thread-to-lane mapping and wave size, 0x3fff when every check passes on
// 32-lane waves (bit 0: the wave size).
// dxc -T cs_6_0 -E main -Vn g_agx_d3d12_probe_cs_wave_dxil -Fh agx_d3d12_probe_cs_wave_dxil.h
RWByteAddressBuffer output : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const uint lane = WaveGetLaneIndex(), n = WaveGetLaneCount();
    const uint all = n >= 32u ? 0xffffffffu : (1u << n) - 1u;
    uint m = n == 32u ? 1u : 0u;
    m |= WaveActiveCountBits(true) == n ? 2u : 0u;
    m |= WavePrefixCountBits(true) == lane ? 4u : 0u;
    m |= WaveActiveSum(1u) == n ? 8u : 0u;
    m |= WavePrefixSum(1u) == lane ? 16u : 0u;
    m |= WaveReadLaneFirst(lane) == 0u ? 32u : 0u;
    m |= WaveReadLaneAt(lane, 1u) == 1u ? 64u : 0u;
    m |= WaveActiveMax(lane) == n - 1u && WaveActiveMin(lane) == 0u ? 128u : 0u;
    m |= WaveIsFirstLane() == (lane == 0u) ? 256u : 0u;
    m |= WaveActiveBallot(true).x == all && WaveActiveBallot(lane < 3u).x == 7u ? 512u : 0u;
    m |= WaveActiveAllTrue(true) && !WaveActiveAnyTrue(false) && WaveActiveAllEqual(n) ? 1024u : 0u;
    m |= WaveActiveBitOr(1u << lane) == all && WaveActiveBitXor(lane) == 0u ? 2048u : 0u;
    m |= WaveActiveProduct(lane == n - 1u ? 3u : 1u) == 3u ? 4096u : 0u;
    m |= WavePrefixProduct(2u) == (1u << lane) ? 8192u : 0u;
    output.Store(id.x * 4, m);
}

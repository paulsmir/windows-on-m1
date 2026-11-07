// D3D12 C52A probe: the mandatory SM 6.4 packed dot products and SM 6.5 wave
// match / multi-prefix operations in 64-thread groups; each thread stores a
// mask of checks that hold for any wave size and lane mapping, 0x3f when all
// pass. The partitions are lane & 3 (WaveMatch of lane % 4).
// dxc -T cs_6_5 -E main -Vn g_agx_d3d12_probe_cs_sm65_dxil -Fh agx_d3d12_probe_cs_sm65_dxil.h
RWByteAddressBuffer output : register(u0);

uint below(uint4 mask, uint lane)
{
    // lanes of the partition below this one (waves of up to 128 lanes)
    uint count = 0u;
    for (uint word = 0u; word < 4u; ++word) {
        const uint first = word * 32u;
        const uint bits = mask[word];
        if (lane >= first + 32u)
            count += countbits(bits);
        else if (lane > first)
            count += countbits(bits & ((1u << (lane - first)) - 1u));
    }
    return count;
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const uint lane = WaveGetLaneIndex();
    const uint a = (id.x * 0x01020304u) ^ 0x80f0017fu, b = 0x05fa0708u ^ (id.x << 8);
    uint u = 7u;
    int s = (int)id.x - 3;
    for (uint i = 0u; i < 4u; ++i) {
        const uint ea = (a >> (i * 8u)) & 0xffu, eb = (b >> (i * 8u)) & 0xffu;
        u += ea * eb;
        s += ((int)(ea << 24) >> 24) * ((int)(eb << 24) >> 24);
    }
    uint m = dot4add_u8packed(a, b, 7u) == u ? 1u : 0u;
    m |= dot4add_i8packed(a, b, (int)id.x - 3) == s ? 2u : 0u;
    const uint4 mask = WaveMatch(lane & 3u);
    const uint4 own = uint4(lane < 32u ? 1u << lane : 0u, lane >= 32u && lane < 64u ? 1u << (lane - 32u) : 0u,
                            lane >= 64u && lane < 96u ? 1u << (lane - 64u) : 0u, lane >= 96u ? 1u << (lane - 96u) : 0u);
    m |= any(mask & own) ? 4u : 0u;
    const uint lower = below(mask, lane);
    m |= WaveMultiPrefixSum(1u, mask) == lower ? 8u : 0u;
    m |= WaveMultiPrefixCountBits(true, mask) == lower ? 16u : 0u;
    // a partition holds at most 32 lanes (waves of up to 128), lower <= 31
    m |= WaveMultiPrefixProduct(2u, mask) == (1u << lower) ? 32u : 0u;
    output.Store(id.x * 4, m);
}

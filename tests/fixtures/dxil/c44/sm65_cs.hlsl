// C52: the mandatory SM 6.4 packed dot products and SM 6.5 wave match /
// multi-prefix operations (dxc -T cs_6_5 -E main -Fo sm65_cs.dxil); dxil-spirv
// lowers them without vendor extensions. dot2add needs -enable-16bit-types
// (this DXC aborts without it), which Native16BitShaderOps (FALSE) excludes.
RWBuffer<uint> o : register(u0);
[numthreads(32, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const uint a = id.x * 0x01020304u, b = 0x05060708u ^ id.x;
    const int acc = (int)id.x - 3;
    uint r = dot4add_u8packed(a, b, 7u);
    r += (uint)dot4add_i8packed(a, b, acc);
    const uint4 mask = WaveMatch(id.x & 3u);
    r += mask.x;
    r += WaveMultiPrefixSum(id.x, mask);
    r += WaveMultiPrefixProduct(id.x | 1u, mask);
    r += WaveMultiPrefixCountBits((id.x & 1u) != 0, mask);
    r += WaveMultiPrefixBitAnd(id.x, mask) + WaveMultiPrefixBitOr(id.x, mask) + WaveMultiPrefixBitXor(id.x, mask);
    o[id.x] = r;
}

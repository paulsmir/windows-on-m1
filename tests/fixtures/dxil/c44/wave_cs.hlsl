// C48: every SM 6.0 wave intrinsic in a compute shader
RWStructuredBuffer<uint4> outv : register(u0);
RWStructuredBuffer<float4> outf : register(u1);
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint v = id.x * 7u + 3u;
    float f = (float)v * 0.5f;
    uint4 b = WaveActiveBallot(v & 1u);
    uint i = id.x;
    uint4 r;
    r.x = WaveGetLaneIndex() | (WaveGetLaneCount() << 8) | (WaveIsFirstLane() ? 0x10000u : 0u);
    r.y = WaveReadLaneFirst(v) + WaveReadLaneAt(v, 5u) + WaveActiveCountBits(v > 10u) + WavePrefixCountBits(v > 20u);
    r.z = WaveActiveSum(v) ^ WaveActiveProduct(v) ^ WaveActiveMin(v) ^ WaveActiveMax(v) ^ WavePrefixSum(v) ^ WavePrefixProduct(v);
    r.w = WaveActiveBitAnd(v) ^ WaveActiveBitOr(v) ^ WaveActiveBitXor(v) ^ b.x ^ b.y ^
          (WaveActiveAnyTrue(v == 9u) ? 1u : 0u) ^ (WaveActiveAllTrue(v != 0u) ? 2u : 0u) ^ (WaveActiveAllEqual(v) ? 4u : 0u);
    outv[i] = r;
    outf[i] = float4(WaveActiveSum(f), WaveActiveMin(f), WaveActiveMax(f), WavePrefixSum(f));
}

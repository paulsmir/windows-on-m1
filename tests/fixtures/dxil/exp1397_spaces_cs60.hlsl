// Original EXP1397 compiler-only namespace fixture. SPDX-License-Identifier: MIT
ByteAddressBuffer A : register(t0, space2);
ByteAddressBuffer B : register(t0, space7);
RWByteAddressBuffer X : register(u0, space1);
RWByteAddressBuffer Y : register(u0, space9);
cbuffer Params : register(b0, space3)
{
    uint word_count;
    uint seed;
    uint reserved0;
    uint reserved1;
};

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint tid = id.x;
    if (tid >= word_count)
        return;
    uint a = A.Load(tid * 4);
    uint b = B.Load(tid * 4);
    X.Store(tid * 4, 3u * a + 5u * b + seed);
    Y.Store(tid * 4, 7u * a - 2u * b + 11u * seed + tid);
}

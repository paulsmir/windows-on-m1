// C44 parity: unbounded UAV ranges (typed image, raw) through the heap
RWTexture2D<float4> imgs[] : register(u0, space1);
RWByteAddressBuffer raws[] : register(u0, space2);
cbuffer C : register(b0) { uint4 which; };
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    float4 v = imgs[which.x][id.xy];
    imgs[which.y][id.xy] = v * 2.0f;
    uint old;
    raws[which.z].InterlockedAdd(0, 1, old);
    raws[which.z].Store(4 + id.x * 4, old);
    raws[which.w].Store(id.y * 4, raws[which.z].Load(id.y * 4));
}

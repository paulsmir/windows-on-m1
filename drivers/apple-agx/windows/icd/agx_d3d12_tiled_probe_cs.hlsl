// C53B: copies a raw buffer SRV into a raw UAV word by word, so the reads of
// a reserved buffer's unmapped tile go through the SRV path (Tier 2: 0).
ByteAddressBuffer src : register(t0);
RWByteAddressBuffer dst : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    dst.Store(id.x * 4, src.Load(id.x * 4));
}

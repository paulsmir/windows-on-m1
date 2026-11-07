// C53T UAV_NULL: Tier 2 typed UAV accesses of a reserved texture's NULL tile.
// WRITE (defined): store 0xabcd at the centre of tile 0 (mapped) and tile 1
// (NULL: discarded). Otherwise: load both back with a status.
RWTexture2D<uint> tex : register(u0);
RWStructuredBuffer<uint2> outb : register(u1);

[numthreads(2, 1, 1)]
void main(uint id : SV_GroupIndex)
{
    const uint2 at = uint2(64 + id * 128, 64);
#ifdef WRITE
    tex[at] = 0xabcdu;
#else
    uint status;
    uint v = tex.Load(int2(at), status);
    outb[id] = uint2(v, CheckAccessFullyMapped(status) ? 1u : 0u);
#endif
}

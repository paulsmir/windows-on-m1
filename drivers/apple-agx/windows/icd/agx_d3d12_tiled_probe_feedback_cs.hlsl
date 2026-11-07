// C53T: Tier 2 residency feedback. Loads one texel at the centre of each of a
// reserved texture's two standard tiles (tile 0 mapped, tile 1 NULL) with a
// status, and stores the value and CheckAccessFullyMapped(status).
Texture2D<uint> tex : register(t0);
RWStructuredBuffer<uint2> outb : register(u0);

[numthreads(2, 1, 1)]
void main(uint id : SV_GroupIndex)
{
    uint status;
    uint v = tex.Load(int3(64 + id * 128, 64, 0), int2(0, 0), status);
    outb[id] = uint2(v, CheckAccessFullyMapped(status) ? 1u : 0u);
}

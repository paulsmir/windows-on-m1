// C59B (dxc -T ps_6_0): the ROV order log of the 8x8 corner pixels -- per
// pixel a count and the SV_PrimitiveID of every primitive in arrival order
// (33 words), so a failing C59A splits into lost updates, a wrong order or
// wrong primitive IDs.
RasterizerOrderedByteAddressBuffer log : register(u1);
void main(float4 pos : SV_Position, uint id : SV_PrimitiveID)
{
    uint2 p = (uint2)pos.xy;
    if (p.x >= 8 || p.y >= 8)
        return;
    uint base = (p.y * 8 + p.x) * 33 * 4;
    uint n = log.Load(base);
    if (n < 32)
        log.Store(base + 4 + n * 4, id);
    log.Store(base, n + 1);
}

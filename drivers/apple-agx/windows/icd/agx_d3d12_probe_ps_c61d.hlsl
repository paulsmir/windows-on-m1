// C61D (dxc -T ps_6_0): conservative rasterization into a 4x render target --
// a conservatively rasterized pixel has every sample covered, so it adds
// countbits(SV_Coverage) = 4 per touched pixel and primitive.
RWByteAddressBuffer counts : register(u0);
float4 main(float4 pos : SV_Position, uint cov : SV_Coverage) : SV_Target
{
    uint ignored;
    counts.InterlockedAdd(((uint)pos.y * 64 + (uint)pos.x) * 4, countbits(cov), ignored);
    return float4(1, 0, 0, 1);
}

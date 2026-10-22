// D3D11 FL 11_1 probe, D11TIR: UAV-only rendering adds each pixel's covered
// forced samples (SV_Coverage) to its counter.
RWByteAddressBuffer counts : register(u0);

void main(float4 pos : SV_Position, uint cov : SV_Coverage)
{
    uint ignored;
    counts.InterlockedAdd(((uint)pos.y * 64 + (uint)pos.x) * 4, countbits(cov), ignored);
}

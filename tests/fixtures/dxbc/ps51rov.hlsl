// ROV through DXBC SM 5.1 (fxc /T ps_5_1): the typed and raw rasterizer-ordered
// UAVs carry D3D11_SB_RASTERIZER_ORDERED_ACCESS; the translation must take the
// pixel interlock before the first access (dxbc_to_nir rov).
RasterizerOrderedTexture2D<float4> accum : register(u1);
RasterizerOrderedByteAddressBuffer counts : register(u2);
float4 main(float4 pos : SV_Position, float4 color : COLOR0) : SV_Target
{
    uint2 p = (uint2)pos.xy;
    float4 old = accum[p];
    accum[p] = old * 0.5 + color;
    counts.Store(p.x * 4, counts.Load(p.x * 4) + 1);
    return old;
}

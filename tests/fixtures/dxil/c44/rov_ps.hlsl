// 12_1 ROV: an ordered read-modify-write of a rasterizer-ordered view
// (dxc -T ps_6_0 -E main)
RasterizerOrderedTexture2D<uint> order : register(u1);
float4 main(float4 pos : SV_Position, uint id : SV_PrimitiveID) : SV_Target
{
    uint2 p = uint2(pos.xy);
    order[p] = order[p] * 5 + id;
    return float4(1, 0, 0, 1);
}

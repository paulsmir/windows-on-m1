// C59A (dxc -T ps_6_0): an ordered read-modify-write per pixel through a
// rasterizer-ordered view; deterministic only in primitive order.
RasterizerOrderedByteAddressBuffer order : register(u1);
void main(float4 pos : SV_Position, uint id : SV_PrimitiveID)
{
    uint a = ((uint)pos.y * 64 + (uint)pos.x) * 4;
    order.Store(a, order.Load(a) * 5 + id);
}

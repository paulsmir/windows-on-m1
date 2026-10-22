// C61A (dxc -T ps_6_0): conservative rasterization counts each pixel's
// invocations; one per touched pixel per triangle.
RWByteAddressBuffer counts : register(u0);
void main(float4 pos : SV_Position)
{
    uint ignored;
    counts.InterlockedAdd(((uint)pos.y * 64 + (uint)pos.x) * 4, 1, ignored);
}

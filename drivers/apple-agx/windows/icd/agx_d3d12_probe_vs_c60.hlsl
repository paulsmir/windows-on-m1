// C60A (dxc -T vs_6_0): positions in NDC from the vertex buffer.
float4 main(float2 p : POSITION) : SV_Position
{
    return float4(p, 0, 1);
}

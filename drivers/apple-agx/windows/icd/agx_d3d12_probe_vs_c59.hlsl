// C59A (dxc -T vs_6_0): viewport-covering triangles from SV_VertexID, three
// vertices each, so that every pixel sees every primitive in order.
float4 main(uint id : SV_VertexID) : SV_Position
{
    uint v = id % 3;
    float2 uv = float2((v << 1) & 2, v & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

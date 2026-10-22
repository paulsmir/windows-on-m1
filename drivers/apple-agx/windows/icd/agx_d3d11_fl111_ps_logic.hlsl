// D3D11 FL 11_1 probe, logic ops: the C57 constant source.
uint4 main(float4 pos : SV_Position) : SV_Target
{
    return uint4(0x0f, 0xf0, 0x55, 0xaa);
}

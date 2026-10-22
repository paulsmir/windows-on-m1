// D3D11 FL 11_1 probe, D11TIR: positions given in NDC by the probe.
float4 main(float2 p : POSITION) : SV_Position
{
    return float4(p, 0, 1);
}

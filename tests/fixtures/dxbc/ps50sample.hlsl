// sample-interpolated input (MiniEngine ModelViewerPS) -> per-sample shading
float4 main(float4 p : SV_Position, sample float4 c : COLOR0) : SV_Target
{
    return c * 0.5;
}

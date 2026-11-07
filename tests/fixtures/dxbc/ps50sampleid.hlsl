// SV_SampleIndex -> per-sample shading
float4 main(float4 p : SV_Position, uint s : SV_SampleIndex) : SV_Target
{
    return float4(s, 0, 0, 1);
}

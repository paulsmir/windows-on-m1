// The generic forced-sample-count pass (dxbc_tir_lower_fs) on a pixel shader
// the plain translator accepts: SV_Coverage, a centroid input and
// pull-model evaluation. fxc /T ps_5_0 /O3 /E main.
float4 main(float4 pos : SV_Position, centroid float2 uv : TEXCOORD0, uint cov : SV_Coverage) : SV_Target
{
    float2 e = EvaluateAttributeAtSample(uv, 3);
    return float4(uv + e, countbits(cov), 1);
}

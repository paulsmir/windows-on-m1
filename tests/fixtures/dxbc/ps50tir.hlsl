// FL 11_1 forced sample count pixel shader variant: fxc /T ps_5_0 /O3 /E main.
// SV_Coverage, a centroid input, the rasterizer's sample count and
// position, pull-model evaluation and a UAV beside the render target.
RWByteAddressBuffer counts : register(u1);

float4 main(float4 pos : SV_Position, centroid float2 uv : TEXCOORD0, uint cov : SV_Coverage) : SV_Target
{
    uint n = GetRenderTargetSampleCount();
    float2 p = GetRenderTargetSamplePosition(1);
    float2 e = EvaluateAttributeAtSample(uv, 2);
    counts.InterlockedAdd(((uint)pos.y * 64 + (uint)pos.x) * 4, countbits(cov));
    return float4(uv + e + p, n, 1);
}

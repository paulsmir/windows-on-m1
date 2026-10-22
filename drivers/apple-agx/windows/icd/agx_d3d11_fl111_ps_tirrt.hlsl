// D3D11 FL 11_1 probe, D11TIR with a render target: target-independent
// rasterization writes each candidate pixel's covered forced samples,
// added up by ONE + ONE blending into an R32_FLOAT target.
float main(float4 pos : SV_Position, uint cov : SV_Coverage) : SV_Target
{
    return (float)countbits(cov);
}

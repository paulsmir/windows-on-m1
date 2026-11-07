// C48: quad intrinsics and wave ops in a pixel shader
Texture2D<float4> t : register(t0);
SamplerState s : register(s0);
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    float4 c = t.Sample(s, uv);
    float x = QuadReadAcrossX(c.x) + QuadReadAcrossY(c.y) + QuadReadAcrossDiagonal(c.z) + QuadReadLaneAt(c.w, 2u);
    return float4(x, WaveActiveSum(c.x), (float)WaveGetLaneIndex(), WaveIsFirstLane() ? 1.0f : 0.0f);
}

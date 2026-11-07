// C44 parity: a register range indexed dynamically (SM 5.1 / 6.0)
Texture2D<float4> mats[4] : register(t0);
SamplerState samp : register(s0);
cbuffer C : register(b0) { uint which; };
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0
{
    return mats[which & 3].Sample(samp, uv);
}

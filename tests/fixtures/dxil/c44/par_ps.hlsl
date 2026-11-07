// C44 parity: interpolation modes, front face, comparison sampling, two targets, depth, discard
Texture2DArray<float> shadow : register(t0);
Texture2D<float4> albedo : register(t1);
SamplerComparisonState cmp : register(s0);
SamplerState lin : register(s1);
struct PsIn { float4 pos : SV_Position; centroid float2 uv : TEXCOORD0; nointerpolation float4 col : COLOR0; float idx : TEXCOORD1; bool front : SV_IsFrontFace; };
struct PsOut { float4 c0 : SV_Target0; float4 c1 : SV_Target1; float depth : SV_Depth; };
PsOut main(PsIn i)
{
    if (i.col.a < 0.01f) discard;
    PsOut o;
    float s = shadow.SampleCmpLevelZero(cmp, float3(i.uv, i.idx), i.pos.z);
    o.c0 = albedo.Sample(lin, i.uv) * s * (i.front ? 1.0f : 0.5f);
    o.c1 = i.col;
    o.depth = i.pos.z * 0.5f;
    return o;
}

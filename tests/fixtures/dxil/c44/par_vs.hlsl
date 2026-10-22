// C44 parity: vertex inputs at registers, varyings, system values
cbuffer Xf : register(b0) { float4x4 m; };
struct VsIn { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0; uint vid : SV_VertexID; uint iid : SV_InstanceID; };
struct VsOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; nointerpolation float4 col : COLOR0; float idx : TEXCOORD1; };
VsOut main(VsIn i)
{
    VsOut o;
    o.pos = mul(m, float4(i.pos, 1.0f));
    o.uv = i.uv;
    o.col = i.col;
    o.idx = (float)(i.vid + i.iid);
    return o;
}

// C44 parity: a domain shader (control points, patch constants, domain location)
struct CP { float4 pos : SV_Position; float2 uv : TEXCOORD0; float w : TEXCOORD1; };
struct PC { float edge[3] : SV_TessFactor; float inside : SV_InsideTessFactor; float4 extra : TEXCOORD2; };
struct O { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 extra : TEXCOORD2; };
[domain("tri")]
O main(PC pc, float3 bary : SV_DomainLocation, const OutputPatch<CP, 3> p)
{
    O o;
    o.pos = p[0].pos * bary.x + p[1].pos * bary.y + p[2].pos * bary.z;
    o.uv = p[0].uv * bary.x + p[1].uv * bary.y + p[2].uv * bary.z + p[2].w;
    o.extra = pc.extra * pc.edge[1] + pc.inside;
    return o;
}

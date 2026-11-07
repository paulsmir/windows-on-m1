// C44 parity: a hull shader (control point phase, patch constants, tess factors)
struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
struct CP { float4 pos : SV_Position; float2 uv : TEXCOORD0; float w : TEXCOORD1; };
struct PC { float edge[3] : SV_TessFactor; float inside : SV_InsideTessFactor; float4 extra : TEXCOORD2; };
cbuffer C : register(b0) { float4 factors; };
PC patch_main(InputPatch<V, 3> p, uint prim : SV_PrimitiveID)
{
    PC o;
    o.edge[0] = factors.x; o.edge[1] = factors.y; o.edge[2] = factors.z;
    o.inside = factors.w;
    o.extra = p[0].pos + p[1].pos + float4(prim, 0, 0, 0);
    return o;
}
[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("patch_main")]
CP main(InputPatch<V, 3> p, uint i : SV_OutputControlPointID)
{
    CP o;
    o.pos = p[i].pos;
    o.uv = p[i].uv * 2.0f;
    o.w = (float)i;
    return o;
}

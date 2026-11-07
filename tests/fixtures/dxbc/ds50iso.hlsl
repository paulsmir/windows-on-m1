// ds_5_0 isoline domain: the tessellator emits lines, so the TIR/CR expansion
// GS (triangles only) must not take this domain shader (C61B).
struct CP { float4 pos : SV_Position; float4 color : COLOR; };
struct PC { float lines[2] : SV_TessFactor; };
[domain("isoline")]
CP main(PC pc, float2 uv : SV_DomainLocation, const OutputPatch<CP, 2> p)
{
    CP o;
    o.pos = lerp(p[0].pos, p[1].pos, uv.x);
    o.color = p[0].color;
    return o;
}

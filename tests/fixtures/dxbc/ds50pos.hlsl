// ds_5_0 reading hs50pos.hlsl's control points: fxc declares them as plain
// "dcl_input vicp[3][r]" although register 0 is SV_Position (C37, EXP1834).
struct CP { float4 pos : SV_Position; float4 color : COLOR; };
struct PC { float edges[3] : SV_TessFactor; float inside : SV_InsideTessFactor; };
[domain("tri")]
CP main(PC pc, float3 uvw : SV_DomainLocation, const OutputPatch<CP, 3> p)
{
    CP o;
    o.pos = uvw.x * p[0].pos + uvw.y * p[1].pos + uvw.z * p[2].pos;
    o.color = p[0].color;
    return o;
}

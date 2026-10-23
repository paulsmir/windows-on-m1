// C61B (dxc -T ds_6_0): the barycentric position of the factor-1 triangle.
struct CP { float4 pos : SV_Position; };
struct PC { float edges[3] : SV_TessFactor; float inside : SV_InsideTessFactor; };
[domain("tri")]
float4 main(PC pc, float3 uvw : SV_DomainLocation, const OutputPatch<CP, 3> p) : SV_Position
{
    return uvw.x * p[0].pos + uvw.y * p[1].pos + uvw.z * p[2].pos;
}

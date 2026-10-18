// ds_5_0: domain point, patch constants and control points.
struct CP { float3 pos : POSITION; };
struct PC { float edge[3] : SV_TessFactor; float inside : SV_InsideTessFactor; float3 center : CENTER; };
[domain("tri")]
float4 main(PC pc, float3 uvw : SV_DomainLocation, const OutputPatch<CP, 3> p) : SV_Position {
  return float4(p[0].pos * uvw.x + p[1].pos * uvw.y + p[2].pos * uvw.z + pc.center * pc.inside, 1);
}

// hs_5_0: control-point and patch-constant phases.
struct CP { float3 pos : POSITION; };
struct PC { float edge[3] : SV_TessFactor; float inside : SV_InsideTessFactor; float3 center : CENTER; };
cbuffer T : register(b0) { float factor; };
PC patch_constants(InputPatch<CP, 3> p, uint pid : SV_PrimitiveID) {
  PC o; [unroll] for (int e = 0; e < 3; ++e) o.edge[e] = factor + e + pid;
  o.inside = factor; o.center = (p[0].pos + p[1].pos + p[2].pos) / 3; return o;
}
[domain("tri")] [partitioning("fractional_odd")] [outputtopology("triangle_cw")]
[outputcontrolpoints(3)] [patchconstantfunc("patch_constants")] [maxtessfactor(32.0)]
CP main(InputPatch<CP, 3> p, uint i : SV_OutputControlPointID) { CP o; o.pos = p[i].pos * 2; return o; }

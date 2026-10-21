// hs_5_0 pass-through whose control points carry SV_Position (C37, EXP1834):
// no control-point phase, so the output signature names the copied
// registers; ds50pos.hlsl reads the same registers without system values.
cbuffer Factors : register(b0) { float4 factors; };
struct CP { float4 pos : SV_Position; float4 color : COLOR; };
struct PC { float edges[3] : SV_TessFactor; float inside : SV_InsideTessFactor; };
PC pc(InputPatch<CP, 3> p) { PC o; o.edges[0] = o.edges[1] = o.edges[2] = factors.x; o.inside = factors.y; return o; }
[domain("tri")] [partitioning("integer")] [outputtopology("triangle_cw")]
[outputcontrolpoints(3)] [patchconstantfunc("pc")]
CP main(InputPatch<CP, 3> p, uint i : SV_OutputControlPointID) { return p[i]; }

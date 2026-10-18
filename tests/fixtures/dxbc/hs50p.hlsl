// hs_5_0 pass-through: no control-point phase (fxc drops "return p[i]"), an
// isoline domain, tess factors written by a fork phase over two instances.
struct CP { float3 pos : POSITION; };
struct PC { float edge[2] : SV_TessFactor; };
PC pc(InputPatch<CP, 2> p) { PC o; o.edge[0] = 3; o.edge[1] = 5; return o; }
[domain("isoline")] [partitioning("integer")] [outputtopology("line")]
[outputcontrolpoints(2)] [patchconstantfunc("pc")]
CP main(InputPatch<CP, 2> p, uint i : SV_OutputControlPointID) { return p[i]; }

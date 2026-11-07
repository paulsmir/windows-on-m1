// C61B (dxc -T hs_6_0): pass-through triangle patches with every tessellation
// factor 1, so the domain shader emits the original triangles and C61A's
// conservative-rasterization reference applies to the tessellated pipeline.
struct CP { float4 pos : SV_Position; };
struct PC { float edges[3] : SV_TessFactor; float inside : SV_InsideTessFactor; };
PC constants(InputPatch<CP, 3> patch)
{
    PC o;
    o.edges[0] = o.edges[1] = o.edges[2] = 1.0;
    o.inside = 1.0;
    return o;
}
[domain("tri")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("constants")]
CP main(InputPatch<CP, 3> patch, uint i : SV_OutputControlPointID)
{
    return patch[i];
}

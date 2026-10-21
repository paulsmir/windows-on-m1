// D3D12 C37 probe: one triangle patch through a hull and a domain shader.
// The edge/inside factors come from a root CBV b0 visible to the hull stage
// only (factor 0 would cull the patch), the colour from a root CBV b1
// visible to the domain stage only. The hull shader's control point phase
// copies its input, so fxc emits no control point phase and the driver's
// pass-through registers carry the patch. The VS/PS are
// agx_d3d12_probe_tri.hlsl (g_agx_d3d12_probe_vs/ps).
// fxc /T hs_5_0 /E hs_main /Vn g_agx_d3d12_probe_hs -> agx_d3d12_probe_hs.h
// fxc /T ds_5_0 /E ds_main /Vn g_agx_d3d12_probe_ds -> agx_d3d12_probe_ds.h
cbuffer Factors : register(b0) { float4 factors; };
cbuffer Tint : register(b1) { float4 tint; };
struct CP { float4 pos : SV_Position; float4 color : COLOR; };
struct PC { float edges[3] : SV_TessFactor; float inside : SV_InsideTessFactor; };

PC hs_patch(InputPatch<CP, 3> p)
{
    PC o;
    o.edges[0] = o.edges[1] = o.edges[2] = factors.x;
    o.inside = factors.y;
    return o;
}

[domain("tri")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("hs_patch")]
CP hs_main(InputPatch<CP, 3> p, uint i : SV_OutputControlPointID)
{
    return p[i];
}

[domain("tri")]
CP ds_main(PC pc, float3 uvw : SV_DomainLocation, const OutputPatch<CP, 3> p)
{
    CP o;
    o.pos = uvw.x * p[0].pos + uvw.y * p[1].pos + uvw.z * p[2].pos;
    o.color = tint;
    return o;
}

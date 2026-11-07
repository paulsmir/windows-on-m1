// C44B: agx_d3d12_probe_vs (vs_5_0 POSITION.xy/COLOR pass-through) as SM 6.0 DXIL.
// dxc -T vs_6_0 -E main -Vn g_agx_d3d12_probe_vs_dxil -Fh agx_d3d12_probe_vs_dxil.h
struct VsIn { float2 pos : POSITION; float4 col : COLOR; };
struct VsOut { float4 pos : SV_Position; float4 col : COLOR; };
VsOut main(VsIn i)
{
    VsOut o;
    o.pos = float4(i.pos, 0.0f, 1.0f);
    o.col = i.col;
    return o;
}

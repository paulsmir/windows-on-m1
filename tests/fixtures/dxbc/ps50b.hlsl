// ps_5_0: attribute evaluation, gather with offsets and comparison, UAV
// writes from a pixel shader, coverage, fine/coarse derivatives (no class
// linkage, no doubles: those stay refused).
Texture2D<float> depth : register(t0); SamplerComparisonState cmp : register(s0); SamplerState s : register(s1);
RWStructuredBuffer<uint> counter : register(u1);
cbuffer D : register(b0) { float4 ref; };
struct PSOut { float4 c : SV_Target; float d : SV_DepthGreaterEqual; uint cov : SV_Coverage; };
PSOut main(noperspective centroid float4 pos : SV_Position, float3 n : NORMAL, float2 uv : TEXCOORD0, uint cov : SV_Coverage) {
  PSOut o;
  float2 c0 = EvaluateAttributeAtSample(uv, 1) + EvaluateAttributeCentroid(uv) + EvaluateAttributeSnapped(uv, int2(1, -1));
  float4 g = depth.GatherCmp(cmp, c0, ref.x, int2(1, 0)) + depth.Gather(s, uv, int2(-1, 1));
  InterlockedAdd(counter[0], 1);
  o.c = float4(n * g.x, g.y) + ddx_fine(uv).xyxy + ddy_coarse(uv).xyxy;
  o.d = pos.z;
  o.cov = cov & 5;
  return o;
}
